/*
 * optimizer.c -- Phase 5: conservative optimization of the Quad IR.
 *
 * The optimizer deliberately works on a private copy.  The Phase 4 array is
 * therefore still available for an honest before/after comparison, while the
 * backend can consume only the optimized copy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "optimizer.h"
#include "symtab.h"

Quad *optimized_quads = 0;
int n_optimized_quads = 0;

int optimization_constant_propagations = 0;
int optimization_constant_folds = 0;
int optimization_cse_eliminated = 0;
int optimization_strength_reductions = 0;
int optimization_dead_instructions = 0;

static int cap_optimized = 0;

static char *sdup(const char *s)
{
    return strdup(s ? s : "");
}

static void free_quad(Quad *q)
{
    free(q->op);
    free(q->arg1);
    free(q->arg2);
    free(q->result);
    q->op = q->arg1 = q->arg2 = q->result = 0;
}

static void clear_stats(void)
{
    optimization_constant_propagations = 0;
    optimization_constant_folds = 0;
    optimization_cse_eliminated = 0;
    optimization_strength_reductions = 0;
    optimization_dead_instructions = 0;
}

static void replace_field(char **field, const char *value)
{
    free(*field);
    *field = sdup(value);
}

static void replace_quad_as_copy(Quad *q, const char *value)
{
    replace_field(&q->op, "=");
    replace_field(&q->arg1, value);
    replace_field(&q->arg2, "");
}

static void copy_original(void)
{
    int i;
    optimizer_free();
    cap_optimized = n_quads > 0 ? n_quads : 1;
    optimized_quads = (Quad *)calloc((size_t)cap_optimized, sizeof(Quad));
    for (i = 0; i < n_quads; i++) {
        optimized_quads[i].op = sdup(quads[i].op);
        optimized_quads[i].arg1 = sdup(quads[i].arg1);
        optimized_quads[i].arg2 = sdup(quads[i].arg2);
        optimized_quads[i].result = sdup(quads[i].result);
    }
    n_optimized_quads = n_quads;
}

void optimizer_free(void)
{
    int i;
    for (i = 0; i < n_optimized_quads; i++)
        free_quad(&optimized_quads[i]);
    free(optimized_quads);
    optimized_quads = 0;
    n_optimized_quads = 0;
    cap_optimized = 0;
}

/* ---------------- scalar constants ---------------- */

#define UINT_MASK 0xffffffffULL

enum { CV_NONE, CV_INT, CV_UINT, CV_FLOAT, CV_BOOL };

typedef struct {
    int kind;
    long long i;
    unsigned long long u;
    double f;
    int float32;
    int b;
} ConstValue;

static long long normalize_int32(long long value)
{
    unsigned long long bits = (unsigned long long)value & UINT_MASK;
    return (bits & 0x80000000ULL) ? (long long)(bits - 0x100000000ULL)
                                  : (long long)bits;
}

static int is_float_text(const char *s)
{
    size_t len = strlen(s);
    int hex_integer = len >= 2 && s[0] == '0' &&
                      (s[1] == 'x' || s[1] == 'X');
    if (hex_integer)
        return 0;
    if (strchr(s, '.') || strchr(s, 'e') || strchr(s, 'E'))
        return 1;
    if (len > 0 &&
        (s[len - 1] == 'f' || s[len - 1] == 'F' ||
         s[len - 1] == 'l' || s[len - 1] == 'L'))
        return 1;
    return 0;
}

static int parse_constant(const char *s, ConstValue *out)
{
    char *tmp, *end;
    size_t len;

    memset(out, 0, sizeof(*out));
    if (!s || !*s)
        return 0;
    if (strcmp(s, "true") == 0) {
        out->kind = CV_BOOL; out->b = 1; return 1;
    }
    if (strcmp(s, "false") == 0) {
        out->kind = CV_BOOL; out->b = 0; return 1;
    }

    len = strlen(s);
    tmp = sdup(s);
    if (len > 0 && (tmp[len - 1] == 'u' || tmp[len - 1] == 'U')) {
        tmp[len - 1] = '\0';
        out->u = strtoull(tmp, &end, 0) & UINT_MASK;
        if (*tmp && *end == '\0') {
            out->kind = CV_UINT;
            free(tmp);
            return 1;
        }
        free(tmp);
        return 0;
    }

    if (is_float_text(s)) {
        int double_literal = len > 0 &&
            (tmp[len - 1] == 'l' || tmp[len - 1] == 'L' ||
             (len >= 2 && (tmp[len - 2] == 'l' || tmp[len - 2] == 'L') &&
              (tmp[len - 1] == 'f' || tmp[len - 1] == 'F')));
        if (len >= 2 && (tmp[len - 2] == 'l' || tmp[len - 2] == 'L')
            && (tmp[len - 1] == 'f' || tmp[len - 1] == 'F'))
            tmp[len - 2] = '\0';
        else if (len > 0 && (tmp[len - 1] == 'f' || tmp[len - 1] == 'F'
                             || tmp[len - 1] == 'l' || tmp[len - 1] == 'L'))
            tmp[len - 1] = '\0';
        out->f = strtod(tmp, &end);
        if (*tmp && *end == '\0') {
            out->kind = CV_FLOAT;
            out->float32 = !double_literal;
            if (out->float32)
                out->f = (double)(float)out->f;
            free(tmp);
            return 1;
        }
        free(tmp);
        return 0;
    }

    out->i = normalize_int32(strtoll(tmp, &end, 0));
    if (*tmp && *end == '\0') {
        out->kind = CV_INT;
        free(tmp);
        return 1;
    }
    free(tmp);
    return 0;
}

static char *format_constant(ConstValue v)
{
    char buf[128];
    switch (v.kind) {
    case CV_INT:
        sprintf(buf, "%lld", normalize_int32(v.i));
        break;
    case CV_UINT:
        sprintf(buf, "%llu", v.u & UINT_MASK);
        strcat(buf, "u");
        break;
    case CV_FLOAT:
        sprintf(buf, v.float32 ? "%.9g" : "%.17g", v.f);
        if (!strchr(buf, '.') && !strchr(buf, 'e') && !strchr(buf, 'E'))
            strcat(buf, ".0");
        break;
    case CV_BOOL:
        strcpy(buf, v.b ? "true" : "false");
        break;
    default:
        strcpy(buf, "");
        break;
    }
    return sdup(buf);
}

typedef struct {
    char *name;
    ConstValue value;
} KnownConstant;

static KnownConstant *known = 0;
static int n_known = 0, cap_known = 0;

static void known_clear(void)
{
    int i;
    for (i = 0; i < n_known; i++)
        free(known[i].name);
    free(known);
    known = 0;
    n_known = cap_known = 0;
}

static KnownConstant *known_find(const char *name)
{
    int i;
    if (!name || !*name)
        return 0;
    for (i = 0; i < n_known; i++)
        if (strcmp(known[i].name, name) == 0)
            return &known[i];
    return 0;
}

static void known_remove(const char *name)
{
    int i;
    KnownConstant *k = known_find(name);
    if (!k)
        return;
    i = (int)(k - known);
    free(known[i].name);
    known[i] = known[--n_known];
}

static void known_set(const char *name, ConstValue value)
{
    KnownConstant *k = known_find(name);
    if (k) {
        k->value = value;
        return;
    }
    if (n_known == cap_known) {
        cap_known = cap_known ? cap_known * 2 : 32;
        known = (KnownConstant *)realloc(known, (size_t)cap_known * sizeof(*known));
    }
    known[n_known].name = sdup(name);
    known[n_known++].value = value;
}

static int is_binary_op(const char *op)
{
    const char *ops[] = { "+", "-", "*", "/", "%", "<<", ">>",
                          "&", "|", "^", "^^", "<", ">", "<=", ">=",
                          "==", "!=", "&&", "||", 0 };
    int i;
    for (i = 0; ops[i]; i++)
        if (strcmp(op, ops[i]) == 0)
            return 1;
    return 0;
}

static int is_unary_op(const char *op)
{
    return strcmp(op, "minus") == 0 || strcmp(op, "!") == 0 || strcmp(op, "~") == 0;
}

static int is_barrier(const char *op)
{
    return strcmp(op, "label") == 0 || strcmp(op, "goto") == 0 ||
           strcmp(op, "if") == 0 || strcmp(op, "ifFalse") == 0 ||
           strcmp(op, "func") == 0 || strcmp(op, "endfunc") == 0 ||
           strcmp(op, "call") == 0 || strcmp(op, "refparam") == 0 ||
           strcmp(op, "return") == 0 || strcmp(op, "discard") == 0;
}

static void propagate_field(char **field)
{
    KnownConstant *k = known_find(*field);
    char *text;
    if (!k)
        return;
    text = format_constant(k->value);
    replace_field(field, text);
    free(text);
    optimization_constant_propagations++;
}

static void propagate_operands(Quad *q)
{
    if (strcmp(q->op, "func") && strcmp(q->op, "endfunc") &&
        strcmp(q->op, "call") && strcmp(q->op, "construct") &&
        strcmp(q->op, "label") && strcmp(q->op, "goto"))
        propagate_field(&q->arg1);

    if (is_binary_op(q->op) || strcmp(q->op, "[]") == 0 ||
        strcmp(q->op, "[]=") == 0)
        propagate_field(&q->arg2);
}

static long long signed_shift_right32(long long value, unsigned long long count)
{
    unsigned long long bits;
    if (count >= 32)
        return value < 0 ? -1 : 0;
    if (value >= 0)
        return (long long)((unsigned long long)value >> count);
    bits = ((unsigned long long)value & UINT_MASK) >> count;
    if (count)
        bits |= UINT_MASK << (32 - count);
    return normalize_int32((long long)bits);
}

static int fold_binary(Quad *q)
{
    ConstValue a, b, r;
    int comparison;
    if (!parse_constant(q->arg1, &a) || !parse_constant(q->arg2, &b))
        return 0;
    memset(&r, 0, sizeof(r));
    comparison = strcmp(q->op, "<") == 0 || strcmp(q->op, ">") == 0 ||
                 strcmp(q->op, "<=") == 0 || strcmp(q->op, ">=") == 0 ||
                 strcmp(q->op, "==") == 0 || strcmp(q->op, "!=") == 0;

    if (strcmp(q->op, "&&") == 0 || strcmp(q->op, "||") == 0 || strcmp(q->op, "^^") == 0) {
        if (a.kind != CV_BOOL || b.kind != CV_BOOL)
            return 0;
        r.kind = CV_BOOL;
        if (!strcmp(q->op, "&&")) r.b = a.b && b.b;
        else if (!strcmp(q->op, "||")) r.b = a.b || b.b;
        else r.b = a.b != b.b;
    } else if (comparison) {
        r.kind = CV_BOOL;
        if (a.kind == CV_FLOAT || b.kind == CV_FLOAT) {
            if (!strcmp(q->op, "<")) r.b = a.f < b.f;
            else if (!strcmp(q->op, ">")) r.b = a.f > b.f;
            else if (!strcmp(q->op, "<=")) r.b = a.f <= b.f;
            else if (!strcmp(q->op, ">=")) r.b = a.f >= b.f;
            else if (!strcmp(q->op, "==")) r.b = a.f == b.f;
            else r.b = a.f != b.f;
        } else if (a.kind == CV_UINT || b.kind == CV_UINT) {
            unsigned long long x = a.kind == CV_UINT ? a.u : ((unsigned long long)a.i & UINT_MASK);
            unsigned long long y = b.kind == CV_UINT ? b.u : ((unsigned long long)b.i & UINT_MASK);
            if (!strcmp(q->op, "<")) r.b = x < y;
            else if (!strcmp(q->op, ">")) r.b = x > y;
            else if (!strcmp(q->op, "<=")) r.b = x <= y;
            else if (!strcmp(q->op, ">=")) r.b = x >= y;
            else if (!strcmp(q->op, "==")) r.b = x == y;
            else r.b = x != y;
        } else if (a.kind == CV_BOOL && b.kind == CV_BOOL) {
            if (!strcmp(q->op, "==")) r.b = a.b == b.b;
            else if (!strcmp(q->op, "!=")) r.b = a.b != b.b;
            else return 0;
        } else {
            if (!strcmp(q->op, "<")) r.b = a.i < b.i;
            else if (!strcmp(q->op, ">")) r.b = a.i > b.i;
            else if (!strcmp(q->op, "<=")) r.b = a.i <= b.i;
            else if (!strcmp(q->op, ">=")) r.b = a.i >= b.i;
            else if (!strcmp(q->op, "==")) r.b = a.i == b.i;
            else r.b = a.i != b.i;
        }
    } else if (a.kind == CV_FLOAT || b.kind == CV_FLOAT) {
        double x = a.kind == CV_FLOAT ? a.f : (a.kind == CV_UINT ? (double)a.u : (double)a.i);
        double y = b.kind == CV_FLOAT ? b.f : (b.kind == CV_UINT ? (double)b.u : (double)b.i);
        if (strcmp(q->op, "/") == 0 && y == 0.0)
            return 0;
        if (strcmp(q->op, "%") == 0 || strcmp(q->op, "<<") == 0 ||
            strcmp(q->op, ">>") == 0 || strcmp(q->op, "&") == 0 ||
            strcmp(q->op, "|") == 0 || strcmp(q->op, "^") == 0)
            return 0;
        r.kind = CV_FLOAT;
        if (!strcmp(q->op, "+")) r.f = x + y;
        else if (!strcmp(q->op, "-")) r.f = x - y;
        else if (!strcmp(q->op, "*")) r.f = x * y;
        else if (!strcmp(q->op, "/")) r.f = x / y;
        else return 0;
        r.float32 = (a.kind == CV_FLOAT && a.float32) ||
                    (b.kind == CV_FLOAT && b.float32);
        if (r.float32)
            r.f = (double)(float)r.f;
    } else {
        int is_shift = strcmp(q->op, "<<") == 0 || strcmp(q->op, ">>") == 0;
        /* GLSL shift result type follows the left operand.  Other integer
         * operators use the usual int/uint promotion represented by the IR. */
        int is_uint = is_shift ? a.kind == CV_UINT
                               : (a.kind == CV_UINT || b.kind == CV_UINT);
        unsigned long long ux = a.kind == CV_UINT ? a.u : ((unsigned long long)a.i & UINT_MASK);
        unsigned long long uy = b.kind == CV_UINT ? b.u : ((unsigned long long)b.i & UINT_MASK);
        if (strcmp(q->op, "/") == 0 && uy == 0)
            return 0;
        if (strcmp(q->op, "%") == 0 && uy == 0)
            return 0;
        if (is_uint) {
            r.kind = CV_UINT;
            if (!strcmp(q->op, "+")) r.u = (ux + uy) & UINT_MASK;
            else if (!strcmp(q->op, "-")) r.u = (ux - uy) & UINT_MASK;
            else if (!strcmp(q->op, "*")) r.u = (ux * uy) & UINT_MASK;
            else if (!strcmp(q->op, "/")) r.u = (ux / uy) & UINT_MASK;
            else if (!strcmp(q->op, "%")) r.u = (ux % uy) & UINT_MASK;
            else if (!strcmp(q->op, "&")) r.u = (ux & uy) & UINT_MASK;
            else if (!strcmp(q->op, "|")) r.u = (ux | uy) & UINT_MASK;
            else if (!strcmp(q->op, "^")) r.u = (ux ^ uy) & UINT_MASK;
            else if (!strcmp(q->op, "<<")) { if (uy >= 32) return 0; r.u = (ux << uy) & UINT_MASK; }
            else if (!strcmp(q->op, ">>")) { if (uy >= 32) return 0; r.u = (ux >> uy) & UINT_MASK; }
            else return 0;
        } else {
            unsigned long long shift = b.kind == CV_UINT ? b.u
                                                          : (unsigned long long)b.i;
            r.kind = CV_INT;
            if (!strcmp(q->op, "+")) r.i = normalize_int32(a.i + b.i);
            else if (!strcmp(q->op, "-")) r.i = normalize_int32(a.i - b.i);
            else if (!strcmp(q->op, "*")) r.i = normalize_int32(a.i * b.i);
            else if (!strcmp(q->op, "/")) {
                if (a.i == -2147483648LL && b.i == -1)
                    return 0;
                r.i = normalize_int32(a.i / b.i);
            }
            else if (!strcmp(q->op, "%")) r.i = normalize_int32(a.i % b.i);
            else if (!strcmp(q->op, "&")) r.i = normalize_int32(a.i & b.i);
            else if (!strcmp(q->op, "|")) r.i = normalize_int32(a.i | b.i);
            else if (!strcmp(q->op, "^")) r.i = normalize_int32(a.i ^ b.i);
            else if (!strcmp(q->op, "<<")) {
                if (shift >= 32) return 0;
                r.i = normalize_int32((long long)(((unsigned long long)a.i & UINT_MASK) << shift));
            }
            else if (!strcmp(q->op, ">>")) {
                if (shift >= 32) return 0;
                r.i = signed_shift_right32(a.i, shift);
            }
            else return 0;
        }
    }
    {
        char *text = format_constant(r);
        replace_quad_as_copy(q, text);
        free(text);
    }
    return 1;
}

static int fold_unary(Quad *q)
{
    ConstValue a, r;
    if (!parse_constant(q->arg1, &a))
        return 0;
    memset(&r, 0, sizeof(r));
    if (strcmp(q->op, "!") == 0) {
        if (a.kind != CV_BOOL) return 0;
        r.kind = CV_BOOL; r.b = !a.b;
    } else if (strcmp(q->op, "minus") == 0) {
        if (a.kind == CV_FLOAT) {
            r.kind = CV_FLOAT; r.f = -a.f; r.float32 = a.float32;
            if (r.float32) r.f = (double)(float)r.f;
        }
        else if (a.kind == CV_INT) { r.kind = CV_INT; r.i = normalize_int32(-a.i); }
        else return 0;
    } else if (strcmp(q->op, "~") == 0) {
        if (a.kind == CV_UINT) { r.kind = CV_UINT; r.u = (~a.u) & UINT_MASK; }
        else if (a.kind == CV_INT) { r.kind = CV_INT; r.i = normalize_int32(~a.i); }
        else return 0;
    } else {
        return 0;
    }
    {
        char *text = format_constant(r);
        replace_quad_as_copy(q, text);
        free(text);
    }
    return 1;
}

static int fold_conversion(Quad *q)
{
    ConstValue a, r;
    Type target;
    if (strcmp(q->op, "conv") != 0 || !parse_constant(q->arg1, &a))
        return 0;
    target = type_from_name(q->arg2);
    if (!is_scalar(target))
        return 0;
    memset(&r, 0, sizeof(r));
    if (target.base == TY_FLOAT || target.base == TY_DOUBLE) {
        r.kind = CV_FLOAT;
        if (a.kind == CV_FLOAT) r.f = a.f;
        else if (a.kind == CV_UINT) r.f = (double)a.u;
        else if (a.kind == CV_INT) r.f = (double)a.i;
        else return 0;
        r.float32 = target.base == TY_FLOAT;
        if (r.float32)
            r.f = (double)(float)r.f;
    } else if (target.base == TY_UINT) {
        r.kind = CV_UINT;
        if (a.kind == CV_UINT) r.u = a.u & UINT_MASK;
        else if (a.kind == CV_INT) r.u = (unsigned long long)a.i & UINT_MASK;
        else return 0;
    } else if (target.base == TY_INT) {
        r.kind = CV_INT;
        if (a.kind == CV_INT) r.i = a.i;
        else if (a.kind == CV_UINT) r.i = normalize_int32((long long)a.u);
        else return 0;
    } else if (target.base == TY_BOOL) {
        r.kind = CV_BOOL;
        if (a.kind == CV_BOOL) r.b = a.b;
        else if (a.kind == CV_INT) r.b = a.i != 0;
        else if (a.kind == CV_UINT) r.b = a.u != 0;
        else if (a.kind == CV_FLOAT) r.b = a.f != 0.0;
        else return 0;
    } else {
        return 0;
    }
    {
        char *text = format_constant(r);
        replace_quad_as_copy(q, text);
        free(text);
    }
    return 1;
}

static void update_known(Quad *q)
{
    ConstValue v;
    if (!q->result[0])
        return;
    if (strcmp(q->op, "=") == 0 && parse_constant(q->arg1, &v))
        known_set(q->result, v);
    else if (strcmp(q->op, "member=") == 0 || strcmp(q->op, "swizzle=") == 0 ||
             strcmp(q->op, "[]=") == 0)
        known_remove(q->result);
    else if (strcmp(q->op, "label") && strcmp(q->op, "goto") &&
             strcmp(q->op, "if") && strcmp(q->op, "ifFalse") &&
             strcmp(q->op, "func") && strcmp(q->op, "endfunc"))
        known_remove(q->result);
}

static void constant_pass(void)
{
    int i, folded;
    known_clear();
    for (i = 0; i < n_optimized_quads; i++) {
        Quad *q = &optimized_quads[i];
        if (is_barrier(q->op))
            known_clear();
        propagate_operands(q);
        folded = 0;
        if (is_binary_op(q->op)) folded = fold_binary(q);
        else if (is_unary_op(q->op)) folded = fold_unary(q);
        else if (strcmp(q->op, "conv") == 0) folded = fold_conversion(q);
        if (folded)
            optimization_constant_folds++;
        update_known(q);
        if (is_barrier(q->op))
            known_clear();
    }
    known_clear();
}

/* ---------------- common subexpressions ---------------- */

typedef struct {
    char *op, *arg1, *arg2, *result;
} Expression;

static Expression *expressions = 0;
static int n_expressions = 0, cap_expressions = 0;

static void expressions_clear(void)
{
    int i;
    for (i = 0; i < n_expressions; i++) {
        free(expressions[i].op);
        free(expressions[i].arg1);
        free(expressions[i].arg2);
        free(expressions[i].result);
    }
    free(expressions);
    expressions = 0;
    n_expressions = cap_expressions = 0;
}

static int cse_candidate(const char *op)
{
    return is_binary_op(op) || is_unary_op(op) || strcmp(op, "conv") == 0 ||
           strcmp(op, "swizzle") == 0 || strcmp(op, "member") == 0 ||
           strcmp(op, "[]") == 0;
}

static int expression_matches(Expression *e, Quad *q)
{
    return strcmp(e->op, q->op) == 0 && strcmp(e->arg1, q->arg1) == 0 &&
           strcmp(e->arg2, q->arg2) == 0;
}

static int expression_mentions(Expression *e, const char *name)
{
    return strcmp(e->result, name) == 0 || strcmp(e->arg1, name) == 0 ||
           strcmp(e->arg2, name) == 0;
}

/* A self-updating expression uses the value being overwritten as an input.
 * It must not be cached for later CSE, because the cached result would refer
 * to the old value of that name rather than the newly computed value. */
static int expression_self_referential(Quad *q)
{
    return q->result[0] &&
           (!strcmp(q->result, q->arg1) || !strcmp(q->result, q->arg2));
}

static void expressions_invalidate(const char *name)
{
    int i, j;
    if (!name || !*name)
        return;
    for (i = 0; i < n_expressions; ) {
        if (!expression_mentions(&expressions[i], name)) {
            i++;
            continue;
        }
        free(expressions[i].op);
        free(expressions[i].arg1);
        free(expressions[i].arg2);
        free(expressions[i].result);
        for (j = i + 1; j < n_expressions; j++)
            expressions[j - 1] = expressions[j];
        n_expressions--;
    }
}

static void expressions_add(Quad *q)
{
    if (n_expressions == cap_expressions) {
        cap_expressions = cap_expressions ? cap_expressions * 2 : 32;
        expressions = (Expression *)realloc(expressions, (size_t)cap_expressions * sizeof(*expressions));
    }
    expressions[n_expressions].op = sdup(q->op);
    expressions[n_expressions].arg1 = sdup(q->arg1);
    expressions[n_expressions].arg2 = sdup(q->arg2);
    expressions[n_expressions++].result = sdup(q->result);
}

static void cse_pass(void)
{
    int i, j;
    expressions_clear();
    for (i = 0; i < n_optimized_quads; i++) {
        Quad *q = &optimized_quads[i];
        if (is_barrier(q->op)) {
            expressions_clear();
            continue;
        }
        if (!cse_candidate(q->op)) {
            if (strcmp(q->op, "member=") == 0 || strcmp(q->op, "swizzle=") == 0 ||
                strcmp(q->op, "[]=") == 0)
                expressions_clear();
            else
                expressions_invalidate(q->result);
            continue;
        }
        expressions_invalidate(q->result);
        for (j = 0; j < n_expressions; j++)
            if (expression_matches(&expressions[j], q))
                break;
        if (!expression_self_referential(q) &&
            j < n_expressions && strcmp(expressions[j].result, q->result) != 0) {
            replace_quad_as_copy(q, expressions[j].result);
            optimization_cse_eliminated++;
        } else if (!expression_self_referential(q)) {
            expressions_add(q);
        }
    }
    expressions_clear();
}

/* ---------------- strength reduction ---------------- */

static int literal_is_two(const char *text, int *is_uint)
{
    ConstValue v;
    if (!parse_constant(text, &v))
        return 0;
    if (v.kind == CV_INT && v.i == 2) { *is_uint = 0; return 1; }
    if (v.kind == CV_UINT && v.u == 2) { *is_uint = 1; return 1; }
    return 0;
}

static int literal_is_one(const char *text, int *is_uint)
{
    ConstValue v;
    if (!parse_constant(text, &v))
        return 0;
    if (v.kind == CV_INT && v.i == 1) { *is_uint = 0; return 1; }
    if (v.kind == CV_UINT && v.u == 1) { *is_uint = 1; return 1; }
    return 0;
}

static int known_uint_operand(const char *text, const char *scope_name)
{
    Symbol *s = sym_lookup_ir_scope(text, scope_name);
    return s && s->type.base == TY_UINT && s->type.arr == 0;
}

static int unsigned_expression(Quad *q, const char *scope_name)
{
    return known_uint_operand(q->arg1, scope_name) ||
           known_uint_operand(q->arg2, scope_name);
}

static void strength_reduction_pass(void)
{
    int i, u1, u2;
    const char *scope_name = 0;
    for (i = 0; i < n_optimized_quads; i++) {
        Quad *q = &optimized_quads[i];
        if (strcmp(q->op, "func") == 0) {
            scope_name = q->arg1;
            continue;
        }
        if (strcmp(q->op, "endfunc") == 0) {
            scope_name = 0;
            continue;
        }
        if (strcmp(q->op, "*") == 0 && literal_is_two(q->arg2, &u2) && u2 &&
            unsigned_expression(q, scope_name)) {
            /* Unsigned x * 2 and x + x have the same modulo arithmetic. */
            replace_field(&q->op, "+");
            replace_field(&q->arg2, q->arg1);
            optimization_strength_reductions++;
        } else if (strcmp(q->op, "*") == 0 && literal_is_two(q->arg1, &u1) && u1 &&
                   unsigned_expression(q, scope_name)) {
            replace_field(&q->op, "+");
            replace_field(&q->arg1, q->arg2);
            optimization_strength_reductions++;
        } else if (strcmp(q->op, "<<") == 0 && literal_is_one(q->arg2, &u2) && u2 &&
                   unsigned_expression(q, scope_name)) {
            /* Unsigned x << 1 and x * 2 have the same modulo arithmetic. */
            replace_field(&q->op, "*");
            replace_field(&q->arg2, u2 ? "2u" : "2");
            optimization_strength_reductions++;
        } else if (strcmp(q->op, "/") == 0 && strcmp(q->arg2, "2u") == 0 &&
                   unsigned_expression(q, scope_name)) {
            /* Unsigned division by two is exactly an unsigned right shift. */
            replace_field(&q->op, ">>");
            replace_field(&q->arg2, "1u");
            optimization_strength_reductions++;
        }
    }
}

/* ---------------- conservative dead temporary elimination ---------------- */

static int is_temp_name(const char *s)
{
    int i;
    if (!s || s[0] != 't' || !s[1])
        return 0;
    for (i = 1; s[i]; i++)
        if (s[i] < '0' || s[i] > '9')
            return 0;
    return 1;
}

static int pure_temp_instruction(Quad *q)
{
    if (!is_temp_name(q->result))
        return 0;
    return strcmp(q->op, "=") == 0 || is_binary_op(q->op) || is_unary_op(q->op) ||
           strcmp(q->op, "conv") == 0 || strcmp(q->op, "swizzle") == 0 ||
           strcmp(q->op, "member") == 0 || strcmp(q->op, "[]") == 0;
}

static void count_use(char *name, int *uses)
{
    int index;
    if (!is_temp_name(name))
        return;
    index = atoi(name + 1);
    if (index >= 0 && index < 1024)
        uses[index]++;
}

static void dead_code_pass(void)
{
    int *uses, i, changed;
    do {
        changed = 0;
        uses = (int *)calloc(1024, sizeof(int));
        for (i = 0; i < n_optimized_quads; i++) {
            Quad *q = &optimized_quads[i];
            if (strcmp(q->op, "func") && strcmp(q->op, "endfunc") &&
                strcmp(q->op, "call") && strcmp(q->op, "construct") &&
                strcmp(q->op, "label") && strcmp(q->op, "goto"))
                count_use(q->arg1, uses);
            if (is_binary_op(q->op) || strcmp(q->op, "[]") == 0 ||
                strcmp(q->op, "[]=") == 0)
                count_use(q->arg2, uses);
        }
        for (i = n_optimized_quads - 1; i >= 0; i--) {
            Quad *q = &optimized_quads[i];
            int index = is_temp_name(q->result) ? atoi(q->result + 1) : -1;
            if (pure_temp_instruction(q) && index >= 0 && index < 1024 && uses[index] == 0) {
                int j;
                free_quad(q);
                for (j = i + 1; j < n_optimized_quads; j++)
                    optimized_quads[j - 1] = optimized_quads[j];
                n_optimized_quads--;
                optimization_dead_instructions++;
                changed = 1;
            }
        }
        free(uses);
    } while (changed);
}

/* ---------------- public optimization entry point ---------------- */

void optimize_ir(int enabled)
{
    clear_stats();
    copy_original();
    if (!enabled)
        return;
    constant_pass();
    cse_pass();
    strength_reduction_pass();
    dead_code_pass();
}

/* Keep the TAC spelling consistent with Phase 4 while accepting any
 * transformed Quad produced by the optimizer. */
static void tac_text(Quad *q, char *out)
{
    const char *op = q->op;
    if (!strcmp(op, "label")) sprintf(out, "%s:", q->result);
    else if (!strcmp(op, "func")) sprintf(out, "func %s:", q->arg1);
    else if (!strcmp(op, "endfunc")) sprintf(out, "endfunc %s", q->arg1);
    else if (!strcmp(op, "goto")) sprintf(out, "    goto %s", q->result);
    else if (!strcmp(op, "if")) sprintf(out, "    if %s goto %s", q->arg1, q->result);
    else if (!strcmp(op, "ifFalse")) sprintf(out, "    ifFalse %s goto %s", q->arg1, q->result);
    else if (!strcmp(op, "=")) sprintf(out, "    %s = %s", q->result, q->arg1);
    else if (!strcmp(op, "conv")) sprintf(out, "    %s = (%s) %s", q->result, q->arg2, q->arg1);
    else if (!strcmp(op, "minus")) sprintf(out, "    %s = -%s", q->result, q->arg1);
    else if (!strcmp(op, "!") || !strcmp(op, "~")) sprintf(out, "    %s = %s%s", q->result, op, q->arg1);
    else if (!strcmp(op, "swizzle") || !strcmp(op, "member")) sprintf(out, "    %s = %s.%s", q->result, q->arg1, q->arg2);
    else if (!strcmp(op, "swizzle=") || !strcmp(op, "member=")) sprintf(out, "    %s.%s = %s", q->result, q->arg2, q->arg1);
    else if (!strcmp(op, "[]")) sprintf(out, "    %s = %s[%s]", q->result, q->arg1, q->arg2);
    else if (!strcmp(op, "[]=")) sprintf(out, "    %s[%s] = %s", q->result, q->arg2, q->arg1);
    else if (!strcmp(op, "param")) sprintf(out, "    param %s", q->arg1);
    else if (!strcmp(op, "refparam")) sprintf(out, "    refparam %s", q->arg1);
    else if (!strcmp(op, "call")) {
        if (q->result[0]) sprintf(out, "    %s = call %s, %s", q->result, q->arg1, q->arg2);
        else sprintf(out, "    call %s, %s", q->arg1, q->arg2);
    } else if (!strcmp(op, "construct")) sprintf(out, "    %s = construct %s, %s", q->result, q->arg1, q->arg2);
    else if (!strcmp(op, "length")) sprintf(out, "    %s = length %s", q->result, q->arg1);
    else if (!strcmp(op, "return")) sprintf(out, "    return%s%s", q->arg1[0] ? " " : "", q->arg1);
    else if (!strcmp(op, "discard")) sprintf(out, "    discard");
    else sprintf(out, "    %s = %s %s %s", q->result, q->arg1, op, q->arg2);
}

void optimizer_print_tac(void)
{
    int i;
    char line[512];
    for (i = 0; i < n_optimized_quads; i++) {
        tac_text(&optimized_quads[i], line);
        printf("%4d  %s\n", i, line);
    }
}

void optimizer_print_quads(void)
{
    int i;
    printf("%-5s | %-10s | %-16s | %-12s | %s\n", "#", "op", "arg1", "arg2", "result");
    printf("------+------------+------------------+--------------+-----------------\n");
    for (i = 0; i < n_optimized_quads; i++)
        printf("%-5d | %-10s | %-16s | %-12s | %s\n", i, optimized_quads[i].op,
               optimized_quads[i].arg1, optimized_quads[i].arg2, optimized_quads[i].result);
}

void optimizer_print_stats(void)
{
    printf("Original instructions  : %d\n", n_quads);
    printf("Optimized instructions : %d\n", n_optimized_quads);
    printf("\nConstant folds                : %d\n", optimization_constant_folds);
    printf("Constant propagations         : %d\n", optimization_constant_propagations);
    printf("Common subexpressions removed : %d\n", optimization_cse_eliminated);
    printf("Strength reductions           : %d\n", optimization_strength_reductions);
    printf("Dead instructions removed     : %d\n", optimization_dead_instructions);
}
