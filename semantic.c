/*
 * semantic.c -- Phase 3: semantic analysis.
 *
 * Walks the AST built by the parser and
 *   - maintains a scoped symbol table (symtab.c): globals, functions,
 *     parameters, block-local variables, struct types, built-ins;
 *   - reports undeclared / redeclared names;
 *   - computes the type of every expression and checks the GLSL typing
 *     rules for scalars, vectors, matrices, swizzles, struct members,
 *     arrays, constructors, built-in and user function calls;
 *   - checks qualifiers (const, uniform, in, out, inout) and lvalues;
 *   - checks statements: conditions are bool, break/continue/case are
 *     inside a loop/switch, return types match the function.
 *
 * Implicit conversions (int -> uint -> float -> double) are made
 * explicit by inserting N_CONV nodes, so the code generator sees a
 * fully typed tree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ast.h"
#include "symtab.h"
#include "semantic.h"

int semantic_errors = 0;
int semantic_warnings = 0;
extern int ast_typed;

static Symbol *cur_func = 0;
static int loop_depth = 0;
static int switch_depth = 0;

/* case labels of the switch statements being checked */
#define MAX_CASES 256
static long case_vals[16][MAX_CASES];
static int  n_case_vals[16];
static int  has_default[16];

/* user functions that are called (checked for a body at the end) */
#define MAX_CALLS 512
static struct { Symbol *f; int line; } called[MAX_CALLS];
static int n_called = 0;

static Type check_expr(Node *n);
static void check_stmt(Node *n);
static void check_stmt_list(Node *list);

/* ---------------- diagnostics ---------------- */

static void sem_error(int line, const char *fmt, ...)
{
    va_list ap;
    if (line > 0)
        fprintf(stderr, "line %d: semantic error: ", line);
    else
        fprintf(stderr, "semantic error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    semantic_errors++;
}

static void sem_warning(int line, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "line %d: warning: ", line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    semantic_warnings++;
}

/* ---------------- helpers ---------------- */

/* Make *slot have type `to`, inserting an implicit conversion if allowed. */
static int coerce(Node **slot, Type to)
{
    Node *e = *slot, *conv;
    if (is_error(e->type) || is_error(to))
        return 1;               /* already reported */
    if (type_equal(e->type, to))
        return 1;
    if (!can_convert(e->type, to))
        return 0;
    conv = node1(N_CONV, e->line, e);
    conv->type = to;
    conv->next = e->next;
    e->next = 0;
    *slot = conv;
    return 1;
}

/* evaluate a constant integer expression (array sizes, case labels) */
static int const_eval(Node *n, long *out)
{
    long a, b;
    if (n == 0)
        return 0;
    switch (n->kind) {
    case N_INTLIT: case N_UINTLIT:
        *out = strtol(n->sval, 0, 0);
        return 1;
    case N_ID:
        if (n->sym && n->sym->has_const) {
            *out = n->sym->const_val;
            return 1;
        }
        return 0;
    case N_CONV:
        return is_integer(n->type) && const_eval(n->a, out);
    case N_UNARY:
        if (!const_eval(n->a, &a))
            return 0;
        if (n->op == OP_NEG)  { *out = -a; return 1; }
        if (n->op == OP_POS)  { *out = a;  return 1; }
        if (n->op == OP_BNOT) { *out = ~a; return 1; }
        return 0;
    case N_BINARY:
        if (!const_eval(n->a, &a) || !const_eval(n->b, &b))
            return 0;
        switch (n->op) {
        case OP_ADD: *out = a + b; return 1;
        case OP_SUB: *out = a - b; return 1;
        case OP_MUL: *out = a * b; return 1;
        case OP_DIV: if (b == 0) return 0; *out = a / b; return 1;
        case OP_MOD: if (b == 0) return 0; *out = a % b; return 1;
        case OP_SHL: *out = a << b; return 1;
        case OP_SHR: *out = a >> b; return 1;
        case OP_BAND: *out = a & b; return 1;
        case OP_BOR:  *out = a | b; return 1;
        case OP_BXOR: *out = a ^ b; return 1;
        }
        return 0;
    default:
        return 0;
    }
}

/* apply the array dimensions of a declaration to its base type */
static Type resolve_type(Type t, Node *dims, int line)
{
    long size;

    if (t.base == TY_STRUCT && !struct_lookup(t.name)) {
        sem_error(line, "unknown type '%s'", t.name);
        return error_type();
    }
    if (dims == 0)
        return t;
    if (dims->next)
        sem_error(line, "arrays of arrays are not supported");
    if (dims->a == 0) {
        t.arr = -1;
        return t;
    }
    check_expr(dims->a);
    if (!const_eval(dims->a, &size)) {
        sem_error(line, "array size must be a constant integer expression");
        t.arr = 1;
    } else if (size <= 0) {
        sem_error(line, "array size must be greater than zero (got %ld)", size);
        t.arr = 1;
    } else {
        t.arr = (int)size;
    }
    return t;
}

/* Can expression e be assigned to? If not, explain why. */
static int writable(Node *e, char **why)
{
    Symbol *s;
    int i, j;
    switch (e->kind) {
    case N_ID:
        s = e->sym;
        if (s == 0)
            return 1;           /* undeclared: already reported */
        if (s->quals & Q_CONST)   { *why = "it is const"; return 0; }
        if (s->quals & Q_UNIFORM) { *why = "it is a uniform"; return 0; }
        if (s->kind == SYM_VAR && s->depth == 0 && (s->quals & Q_IN)) {
            *why = "it is a shader input ('in')";
            return 0;
        }
        if (s->readonly) { *why = "it is a read-only built-in"; return 0; }
        return 1;
    case N_INDEX:
        return writable(e->a, why);
    case N_FIELD:
        if (e->is_swizzle)
            for (i = 0; e->sval[i]; i++)
                for (j = i + 1; e->sval[j]; j++)
                    if (e->sval[i] == e->sval[j]) {
                        *why = "the swizzle repeats a component";
                        return 0;
                    }
        return writable(e->a, why);
    case N_CONV:
        return writable(e->a, why);
    default:
        *why = "it is not an lvalue";
        return 0;
    }
}

static int check_lvalue(Node *e, const char *what)
{
    char *why = "";
    if (is_error(e->type))
        return 0;
    if (!writable(e, &why)) {
        sem_error(e->line, "cannot %s this expression: %s", what, why);
        return 0;
    }
    e->lvalue = 1;
    return 1;
}

/* ---------------- operators ---------------- */

/* shape of "L op R" when both have the same base type; 0 if illegal */
static int shape_result(int op, Type L, Type R, Type *out)
{
    if (is_scalar(L)) { *out = R; return 1; }
    if (is_scalar(R)) { *out = L; return 1; }
    if (is_vector(L) && is_vector(R)) {
        if (L.vec != R.vec)
            return 0;
        *out = L;
        return 1;
    }
    if (op == OP_MUL) {
        if (is_matrix(L) && is_vector(R)) {          /* mat * column vector */
            if (L.cols != R.vec) return 0;
            *out = mk_type(L.base, L.vec, 0);
            return 1;
        }
        if (is_vector(L) && is_matrix(R)) {          /* row vector * mat */
            if (L.vec != R.vec) return 0;
            *out = mk_type(L.base, R.cols, 0);
            return 1;
        }
        if (is_matrix(L) && is_matrix(R)) {          /* linear-algebra product */
            if (L.cols != R.vec) return 0;
            *out = mk_type(L.base, L.vec, R.cols);
            return 1;
        }
        return 0;
    }
    if (is_matrix(L) && is_matrix(R) && L.vec == R.vec && L.cols == R.cols) {
        *out = L;
        return 1;
    }
    return 0;
}

static Type check_binary(Node *n)
{
    Type L = check_expr(n->a);
    Type R = check_expr(n->b);
    Type res;
    BaseType b;

    if (is_error(L) || is_error(R))
        return error_type();

    switch (n->op) {
    case OP_LAND: case OP_LOR: case OP_LXOR:
        if (!is_bool(L) || !is_bool(R)) {
            sem_error(n->line, "operands of '%s' must be bool, got %s and %s",
                      op_str(n->op), type_str(L), type_str(R));
            return error_type();
        }
        return mk_type(TY_BOOL, 1, 0);

    case OP_EQ: case OP_NE:
        if (L.base == TY_SAMPLER || L.base == TY_IMAGE || is_array(L) || is_array(R)) {
            sem_error(n->line, "'%s' cannot compare values of type %s", op_str(n->op), type_str(L));
            return error_type();
        }
        if (!type_equal(L, R)) {
            if (can_convert(L, R))
                coerce(&n->a, R);
            else if (can_convert(R, L))
                coerce(&n->b, L);
            else {
                sem_error(n->line, "cannot compare %s with %s", type_str(L), type_str(R));
                return error_type();
            }
        }
        return mk_type(TY_BOOL, 1, 0);

    case OP_LT: case OP_GT: case OP_LE: case OP_GE:
        if (!is_scalar(L) || !is_scalar(R) || !is_numeric(L) || !is_numeric(R)) {
            sem_error(n->line, "'%s' needs numeric scalar operands, got %s and %s "
                      "(use lessThan()/greaterThan() for vectors)",
                      op_str(n->op), type_str(L), type_str(R));
            return error_type();
        }
        b = base_rank(L.base) > base_rank(R.base) ? L.base : R.base;
        coerce(&n->a, with_base(L, b));
        coerce(&n->b, with_base(R, b));
        return mk_type(TY_BOOL, 1, 0);

    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        if (!is_numeric(L) || !is_numeric(R)) {
            sem_error(n->line, "operator '%s' needs numeric operands, got %s and %s",
                      op_str(n->op), type_str(L), type_str(R));
            return error_type();
        }
        b = base_rank(L.base) > base_rank(R.base) ? L.base : R.base;
        L = with_base(L, b);
        R = with_base(R, b);
        if (!shape_result(n->op, L, R, &res)) {
            sem_error(n->line, "incompatible operands for '%s': %s and %s",
                      op_str(n->op), type_str(n->a->type), type_str(n->b->type));
            return error_type();
        }
        coerce(&n->a, L);
        coerce(&n->b, R);
        return res;

    case OP_MOD: case OP_BAND: case OP_BOR: case OP_BXOR:
        if (!is_integer(L) || !is_integer(R)) {
            sem_error(n->line, "operator '%s' needs int or uint operands, got %s and %s",
                      op_str(n->op), type_str(L), type_str(R));
            return error_type();
        }
        b = base_rank(L.base) > base_rank(R.base) ? L.base : R.base;
        L = with_base(L, b);
        R = with_base(R, b);
        if (!shape_result(n->op, L, R, &res)) {
            sem_error(n->line, "incompatible operands for '%s': %s and %s",
                      op_str(n->op), type_str(n->a->type), type_str(n->b->type));
            return error_type();
        }
        coerce(&n->a, L);
        coerce(&n->b, R);
        return res;

    case OP_SHL: case OP_SHR:
        if (!is_integer(L) || !is_integer(R) || (is_vector(R) && R.vec != L.vec)
            || (is_scalar(L) && is_vector(R))) {
            sem_error(n->line, "invalid operands for '%s': %s and %s",
                      op_str(n->op), type_str(L), type_str(R));
            return error_type();
        }
        return L;
    }
    sem_error(n->line, "unknown binary operator");
    return error_type();
}

static Type check_unary(Node *n)
{
    Type t = check_expr(n->a);
    if (is_error(t))
        return t;
    switch (n->op) {
    case OP_NEG: case OP_POS:
        if (!is_numeric(t)) {
            sem_error(n->line, "unary '%s' needs a numeric operand, got %s", op_str(n->op), type_str(t));
            return error_type();
        }
        return t;
    case OP_NOT:
        if (!is_bool(t)) {
            sem_error(n->line, "'!' needs a bool operand, got %s (use not() for bvec)", type_str(t));
            return error_type();
        }
        return t;
    case OP_BNOT:
        if (!is_integer(t)) {
            sem_error(n->line, "'~' needs an int or uint operand, got %s", type_str(t));
            return error_type();
        }
        return t;
    case OP_INC: case OP_DEC:
        if (!is_numeric(t)) {
            sem_error(n->line, "'%s' needs a numeric operand, got %s", op_str(n->op), type_str(t));
            return error_type();
        }
        check_lvalue(n->a, n->op == OP_INC ? "increment" : "decrement");
        return t;
    }
    return error_type();
}

static Type check_assign(Node *n)
{
    Type L = check_expr(n->a);
    Type R = check_expr(n->b);
    Type res;

    if (is_error(L) || is_error(R))
        return error_type();
    check_lvalue(n->a, "assign to");

    if (n->op == OP_ASSIGN) {
        if (!coerce(&n->b, L)) {
            sem_error(n->line, "cannot assign a value of type %s to a variable of type %s",
                      type_str(R), type_str(L));
            return error_type();
        }
        return L;
    }

    /* compound assignment: L op= R must have the type of L */
    if (!is_numeric(L) || !is_numeric(R)) {
        sem_error(n->line, "'%s=' needs numeric operands, got %s and %s",
                  op_str(n->op), type_str(L), type_str(R));
        return error_type();
    }
    if ((n->op == OP_MOD || n->op == OP_BAND || n->op == OP_BOR || n->op == OP_BXOR
         || n->op == OP_SHL || n->op == OP_SHR) && (!is_integer(L) || !is_integer(R))) {
        sem_error(n->line, "'%s=' needs int or uint operands, got %s and %s",
                  op_str(n->op), type_str(L), type_str(R));
        return error_type();
    }
    if (n->op == OP_SHL || n->op == OP_SHR)
        return L;
    if (R.base != L.base) {
        if (!coerce(&n->b, with_base(R, L.base))) {
            sem_error(n->line, "cannot convert %s to %s in '%s='",
                      type_str(R), base_str(L.base), op_str(n->op));
            return error_type();
        }
        R = with_base(R, L.base);
    }
    if (!shape_result(n->op, L, R, &res) || !type_equal(res, L)) {
        sem_error(n->line, "'%s %s= %s' does not produce a %s",
                  type_str(L), op_str(n->op), type_str(R), type_str(L));
        return error_type();
    }
    return L;
}

/* ---------------- member access, swizzles, indexing ---------------- */

static int swizzle_index(char c, int *set)
{
    const char *sets[3] = { "xyzw", "rgba", "stpq" };
    int s;
    const char *p;
    for (s = 0; s < 3; s++)
        if ((p = strchr(sets[s], c)) != 0) {
            *set = s;
            return (int)(p - sets[s]);
        }
    return -1;
}

static Type check_field(Node *n)
{
    Type t = check_expr(n->a);
    Type mt;
    StructDef *sd;
    int i, set, first_set = -1, len, idx;

    if (is_error(t))
        return t;

    if (t.base == TY_STRUCT && !is_array(t)) {
        sd = struct_lookup(t.name);
        if (sd && struct_member(sd, n->sval, &mt))
            return mt;
        sem_error(n->line, "'%s' has no member named '%s'", t.name, n->sval);
        return error_type();
    }

    if ((is_vector(t) || is_scalar(t))) {
        n->is_swizzle = 1;
        len = (int)strlen(n->sval);
        if (len > 4) {
            sem_error(n->line, "swizzle '.%s' has more than 4 components", n->sval);
            return error_type();
        }
        for (i = 0; i < len; i++) {
            idx = swizzle_index(n->sval[i], &set);
            if (idx < 0) {
                sem_error(n->line, "invalid swizzle component '%c' in '.%s'", n->sval[i], n->sval);
                return error_type();
            }
            if (first_set >= 0 && set != first_set) {
                sem_error(n->line, "swizzle '.%s' mixes component sets (xyzw / rgba / stpq)", n->sval);
                return error_type();
            }
            first_set = set;
            if (idx >= t.vec) {
                sem_error(n->line, "swizzle '.%s' selects component '%c' beyond %s",
                          n->sval, n->sval[i], type_str(t));
                return error_type();
            }
        }
        return mk_type(t.base, len, 0);
    }

    sem_error(n->line, "request for member '%s' in a value of type %s", n->sval, type_str(t));
    return error_type();
}

static Type check_index(Node *n)
{
    Type t = check_expr(n->a);
    Type it = check_expr(n->b);
    long k;
    int limit = 0;
    Type res;

    if (is_error(t) || is_error(it))
        return error_type();
    if (!is_integer(it) || !is_scalar(it)) {
        sem_error(n->line, "array index must be an int or uint scalar, got %s", type_str(it));
        return error_type();
    }

    if (is_array(t)) {
        res = elem_type(t);
        limit = t.arr;
    } else if (is_matrix(t)) {
        res = mk_type(t.base, t.vec, 0);     /* a column */
        limit = t.cols;
    } else if (is_vector(t)) {
        res = mk_type(t.base, 1, 0);
        limit = t.vec;
    } else {
        sem_error(n->line, "subscripted value of type %s is not an array, vector or matrix", type_str(t));
        return error_type();
    }
    if (limit > 0 && const_eval(n->b, &k) && (k < 0 || k >= limit))
        sem_error(n->line, "index %ld is out of range for %s", k, type_str(t));
    return res;
}

/* ---------------- constructors ---------------- */

static Type check_ctor(Node *n)
{
    Type t = n->type;
    Node *a, **slot;
    int nargs = 0, total = 0, need, i, bad = 0;
    StructDef *sd;

    for (a = n->a; a; a = a->next) {
        if (is_error(check_expr(a)))
            bad = 1;
        nargs++;
    }

    if (n->d) {                         /* array constructor: float[3](a, b, c) */
        t = resolve_type(t, n->d, n->line);
        if (is_error(t) || bad)
            return is_error(t) ? t : (t.arr < 0 ? (t.arr = nargs, t) : t);
        if (t.arr < 0)
            t.arr = nargs;
        else if (nargs != t.arr)
            sem_error(n->line, "array constructor %s() needs %d argument(s), got %d",
                      type_str(t), t.arr, nargs);
        for (i = 0, slot = &n->a; *slot; slot = &(*slot)->next, i++)
            if (!coerce(slot, elem_type(t)))
                sem_error(n->line, "argument %d of %s(): cannot convert %s to %s", i + 1,
                          type_str(t), type_str((*slot)->type), type_str(elem_type(t)));
        return t;
    }
    if (bad)
        return t.base == TY_STRUCT && !struct_lookup(t.name) ? error_type() : t;

    if (t.base == TY_STRUCT) {
        sd = struct_lookup(t.name);
        if (!sd) {
            sem_error(n->line, "unknown type '%s'", t.name);
            return error_type();
        }
        if (nargs != sd->nmembers) {
            sem_error(n->line, "constructor of struct %s needs %d argument(s), got %d",
                      sd->name, sd->nmembers, nargs);
            return t;
        }
        for (i = 0, slot = &n->a; *slot; slot = &(*slot)->next, i++)
            if (!coerce(slot, sd->mtypes[i]))
                sem_error(n->line, "argument %d of %s(): cannot convert %s to %s",
                          i + 1, sd->name, type_str((*slot)->type), type_str(sd->mtypes[i]));
        return t;
    }

    if (!is_scalar(t) && !is_vector(t) && !is_matrix(t)) {
        sem_error(n->line, "cannot construct a value of type %s", type_str(t));
        return error_type();
    }
    if (nargs == 0) {
        sem_error(n->line, "constructor %s() needs at least one argument", type_str(t));
        return t;
    }
    for (a = n->a; a; a = a->next)
        if (!is_scalar(a->type) && !is_vector(a->type) && !is_matrix(a->type)) {
            sem_error(n->line, "cannot use a value of type %s in constructor %s()",
                      type_str(a->type), type_str(t));
            return t;
        }

    /* single scalar: fill (vector) or set the diagonal (matrix) */
    if (nargs == 1 && is_scalar(n->a->type))
        return t;
    /* matrix from matrix: copy the overlapping part */
    if (is_matrix(t) && nargs == 1 && is_matrix(n->a->type))
        return t;
    /* scalar from anything: takes the first component */
    if (is_scalar(t)) {
        if (nargs > 1)
            sem_error(n->line, "too many arguments to constructor %s()", type_str(t));
        return t;
    }

    need = components(t);
    for (a = n->a; a; a = a->next) {
        if (is_matrix(a->type) && is_matrix(t) == 0 && nargs > 1) {
            sem_error(n->line, "a matrix argument must be the only argument of %s()", type_str(t));
            return t;
        }
        if (total >= need) {
            sem_error(n->line, "too many arguments to constructor %s()", type_str(t));
            return t;
        }
        total += components(a->type);
    }
    if (total < need)
        sem_error(n->line, "not enough data for constructor %s(): need %d components, got %d",
                  type_str(t), need, total);
    return t;
}

/* ---------------- built-in functions ---------------- */

enum {
    BF_GEN1, BF_ABS, BF_ATAN, BF_POW, BF_MINMAX, BF_MOD, BF_CLAMP, BF_MIX, BF_STEP,
    BF_SMOOTH, BF_LENGTH, BF_DOT, BF_CROSS, BF_REFLECT, BF_FACEFWD, BF_REFRACT,
    BF_TEXTURE, BF_TEXLOD, BF_TEXFETCH, BF_TEXSIZE,
    BF_TRANSPOSE, BF_INVERSE, BF_DET, BF_MCOMP, BF_OUTER,
    BF_VREL, BF_ANYALL, BF_NOT
};

static struct builtin_fn { char *name; int kind; int min, max; } builtins[] = {
    {"radians", BF_GEN1, 1, 1}, {"degrees", BF_GEN1, 1, 1},
    {"sin", BF_GEN1, 1, 1},     {"cos", BF_GEN1, 1, 1},     {"tan", BF_GEN1, 1, 1},
    {"asin", BF_GEN1, 1, 1},    {"acos", BF_GEN1, 1, 1},    {"atan", BF_ATAN, 1, 2},
    {"sinh", BF_GEN1, 1, 1},    {"cosh", BF_GEN1, 1, 1},    {"tanh", BF_GEN1, 1, 1},
    {"exp", BF_GEN1, 1, 1},     {"log", BF_GEN1, 1, 1},     {"exp2", BF_GEN1, 1, 1},
    {"log2", BF_GEN1, 1, 1},    {"sqrt", BF_GEN1, 1, 1},    {"inversesqrt", BF_GEN1, 1, 1},
    {"floor", BF_GEN1, 1, 1},   {"ceil", BF_GEN1, 1, 1},    {"trunc", BF_GEN1, 1, 1},
    {"round", BF_GEN1, 1, 1},   {"roundEven", BF_GEN1, 1, 1}, {"fract", BF_GEN1, 1, 1},
    {"normalize", BF_GEN1, 1, 1}, {"dFdx", BF_GEN1, 1, 1},  {"dFdy", BF_GEN1, 1, 1},
    {"fwidth", BF_GEN1, 1, 1},
    {"abs", BF_ABS, 1, 1},      {"sign", BF_ABS, 1, 1},
    {"pow", BF_POW, 2, 2},
    {"min", BF_MINMAX, 2, 2},   {"max", BF_MINMAX, 2, 2},   {"mod", BF_MOD, 2, 2},
    {"clamp", BF_CLAMP, 3, 3},  {"mix", BF_MIX, 3, 3},
    {"step", BF_STEP, 2, 2},    {"smoothstep", BF_SMOOTH, 3, 3},
    {"length", BF_LENGTH, 1, 1}, {"distance", BF_DOT, 2, 2}, {"dot", BF_DOT, 2, 2},
    {"cross", BF_CROSS, 2, 2},  {"reflect", BF_REFLECT, 2, 2},
    {"faceforward", BF_FACEFWD, 3, 3}, {"refract", BF_REFRACT, 3, 3},
    {"texture", BF_TEXTURE, 2, 3}, {"texture2D", BF_TEXTURE, 2, 3},
    {"textureCube", BF_TEXTURE, 2, 3}, {"textureLod", BF_TEXLOD, 3, 3},
    {"texelFetch", BF_TEXFETCH, 2, 3}, {"textureSize", BF_TEXSIZE, 1, 2},
    {"transpose", BF_TRANSPOSE, 1, 1}, {"inverse", BF_INVERSE, 1, 1},
    {"determinant", BF_DET, 1, 1}, {"matrixCompMult", BF_MCOMP, 2, 2},
    {"outerProduct", BF_OUTER, 2, 2},
    {"lessThan", BF_VREL, 2, 2}, {"lessThanEqual", BF_VREL, 2, 2},
    {"greaterThan", BF_VREL, 2, 2}, {"greaterThanEqual", BF_VREL, 2, 2},
    {"equal", BF_VREL, 2, 2},   {"notEqual", BF_VREL, 2, 2},
    {"any", BF_ANYALL, 1, 1},   {"all", BF_ANYALL, 1, 1},   {"not", BF_NOT, 1, 1},
    {0, 0, 0, 0}
};

static struct builtin_fn *find_builtin(const char *name)
{
    int i;
    for (i = 0; builtins[i].name; i++)
        if (strcmp(builtins[i].name, name) == 0)
            return &builtins[i];
    return 0;
}

#define MAXA 16
static Node **bslot[MAXA];
static int bn;

/* float version of an argument type ("genType"); error type if impossible */
static Type gen_float(Type t)
{
    if ((is_scalar(t) || is_vector(t)) && t.base != TY_BOOL)
        return t.base == TY_DOUBLE ? t : with_base(t, TY_FLOAT);
    return error_type();
}

static int fit(int i, Type t)
{
    if (is_error(t))
        return 0;
    return coerce(bslot[i], t);
}

static Type A(int i) { return (*bslot[i])->type; }

/* fit argument i to T, or to a scalar of T's base type if it is a scalar */
static int fit_or_scalar(int i, Type T)
{
    if (is_scalar(A(i)))
        return fit(i, mk_type(T.base, 1, 0));
    return fit(i, T);
}

/* coordinate size of a sampler (with_extra: +1 for Shadow compare value) */
static int sampler_dims(char *name, int with_extra)
{
    char *p = strstr(name, "sampler") + 7;
    int d = 2;
    if (strncmp(p, "1D", 2) == 0 || strncmp(p, "Buffer", 6) == 0) d = 1;
    else if (strncmp(p, "3D", 2) == 0 || strncmp(p, "Cube", 4) == 0) d = 3;
    if (strstr(p, "Array")) d++;
    if (with_extra && strstr(p, "Shadow")) d++;
    return d > 4 ? 4 : d;
}

static Type sampler_result(char *name)
{
    if (strstr(name, "Shadow")) return mk_type(TY_FLOAT, 1, 0);
    if (name[0] == 'i') return mk_type(TY_INT, 4, 0);
    if (name[0] == 'u') return mk_type(TY_UINT, 4, 0);
    return mk_type(TY_FLOAT, 4, 0);
}

static Type check_builtin(Node *n, struct builtin_fn *bf)
{
    Type T, R;
    Node **slot;
    int ok = 1, d;

    bn = 0;
    for (slot = &n->a; *slot && bn < MAXA; slot = &(*slot)->next)
        bslot[bn++] = slot;

    if (bn < bf->min || bn > bf->max) {
        sem_error(n->line, "built-in function %s() takes %d%s%d argument(s), got %d",
                  bf->name, bf->min, bf->min == bf->max ? "" : " to ",
                  bf->min == bf->max ? 0 : bf->max, bn);
        return error_type();
    }

    switch (bf->kind) {
    case BF_GEN1:
        T = gen_float(A(0));
        ok = fit(0, T);
        R = T;
        break;
    case BF_ABS:
        if (is_integer(A(0)) && A(0).base == TY_INT) { R = A(0); break; }
        T = gen_float(A(0));
        ok = fit(0, T);
        R = T;
        break;
    case BF_ATAN: case BF_POW:
        T = gen_float(A(0));
        ok = fit(0, T) && (bn < 2 || fit(1, T));
        R = T;
        break;
    case BF_MINMAX: case BF_MOD:
        if (bf->kind == BF_MINMAX && is_integer(A(0)) && is_integer(A(1))
            && A(0).base == A(1).base)
            T = A(0);
        else
            T = gen_float(A(0));
        ok = fit(0, T) && fit_or_scalar(1, T);
        R = T;
        break;
    case BF_CLAMP:
        if (is_integer(A(0)) && is_integer(A(1)) && is_integer(A(2)))
            T = A(0);
        else
            T = gen_float(A(0));
        ok = fit(0, T) && fit_or_scalar(1, T) && fit_or_scalar(2, T);
        R = T;
        break;
    case BF_MIX:
        T = gen_float(A(0));
        ok = fit(0, T) && fit(1, T);
        if (ok && is_bool_any(A(2)))
            ok = is_scalar(A(2)) || A(2).vec == T.vec;
        else if (ok)
            ok = fit_or_scalar(2, T);
        R = T;
        break;
    case BF_STEP:
        T = gen_float(A(1));
        ok = fit(1, T) && fit_or_scalar(0, T);
        R = T;
        break;
    case BF_SMOOTH:
        T = gen_float(A(2));
        ok = fit(2, T) && fit_or_scalar(0, T) && fit_or_scalar(1, T);
        R = T;
        break;
    case BF_LENGTH:
        T = gen_float(A(0));
        ok = fit(0, T);
        R = mk_type(T.base, 1, 0);
        break;
    case BF_DOT:
        T = gen_float(A(0));
        ok = fit(0, T) && fit(1, T);
        R = mk_type(T.base, 1, 0);
        break;
    case BF_CROSS:
        T = mk_type(TY_FLOAT, 3, 0);
        ok = fit(0, T) && fit(1, T);
        R = T;
        break;
    case BF_REFLECT:
        T = gen_float(A(0));
        ok = fit(0, T) && fit(1, T);
        R = T;
        break;
    case BF_FACEFWD:
        T = gen_float(A(0));
        ok = fit(0, T) && fit(1, T) && fit(2, T);
        R = T;
        break;
    case BF_REFRACT:
        T = gen_float(A(0));
        ok = fit(0, T) && fit(1, T) && fit(2, mk_type(T.base, 1, 0));
        R = T;
        break;
    case BF_TEXTURE: case BF_TEXLOD:
        if (A(0).base != TY_SAMPLER || is_array(A(0))) {
            sem_error(n->line, "first argument of %s() must be a sampler, got %s",
                      bf->name, type_str(A(0)));
            return error_type();
        }
        d = sampler_dims(A(0).name, 1);
        ok = fit(1, mk_type(TY_FLOAT, d, 0));
        if (ok && bn == 3)
            ok = fit(2, mk_type(TY_FLOAT, 1, 0));
        R = sampler_result(A(0).name);
        break;
    case BF_TEXFETCH:
        if (A(0).base != TY_SAMPLER || is_array(A(0))) {
            sem_error(n->line, "first argument of texelFetch() must be a sampler, got %s", type_str(A(0)));
            return error_type();
        }
        d = sampler_dims(A(0).name, 0);
        ok = fit(1, mk_type(TY_INT, d, 0)) && (bn < 3 || fit(2, mk_type(TY_INT, 1, 0)));
        R = sampler_result(A(0).name);
        break;
    case BF_TEXSIZE:
        if (A(0).base != TY_SAMPLER || is_array(A(0))) {
            sem_error(n->line, "first argument of textureSize() must be a sampler, got %s", type_str(A(0)));
            return error_type();
        }
        ok = bn < 2 || fit(1, mk_type(TY_INT, 1, 0));
        R = mk_type(TY_INT, sampler_dims(A(0).name, 0), 0);
        break;
    case BF_TRANSPOSE:
        ok = is_matrix(A(0));
        R = mk_type(A(0).base, A(0).cols, A(0).vec);
        break;
    case BF_INVERSE:
        ok = is_matrix(A(0)) && A(0).vec == A(0).cols;
        R = A(0);
        break;
    case BF_DET:
        ok = is_matrix(A(0)) && A(0).vec == A(0).cols;
        R = mk_type(A(0).base, 1, 0);
        break;
    case BF_MCOMP:
        ok = is_matrix(A(0)) && fit(1, A(0));
        R = A(0);
        break;
    case BF_OUTER:
        ok = is_vector(A(0)) && is_vector(A(1)) && is_float_based(A(0)) && is_float_based(A(1));
        R = mk_type(A(0).base, A(0).vec, A(1).vec);
        break;
    case BF_VREL:
        ok = is_vector(A(0)) && is_vector(A(1));
        if (ok && !type_equal(A(0), A(1))) {
            if (can_convert(A(0), A(1))) ok = fit(0, A(1));
            else ok = fit(1, A(0));
        }
        if (ok && is_bool_any(A(0)) && strcmp(bf->name, "equal") && strcmp(bf->name, "notEqual"))
            ok = 0;
        R = mk_type(TY_BOOL, A(0).vec, 0);
        break;
    case BF_ANYALL:
        ok = is_bool_any(A(0)) && is_vector(A(0));
        R = mk_type(TY_BOOL, 1, 0);
        break;
    case BF_NOT:
        ok = is_bool_any(A(0)) && is_vector(A(0));
        R = A(0);
        break;
    default:
        ok = 0;
    }

    if (!ok) {
        char buf[256];
        int i;
        buf[0] = '\0';
        for (i = 0; i < bn; i++)
            sprintf(buf + strlen(buf), "%s%s", i ? ", " : "", type_str((*bslot[i])->type));
        sem_error(n->line, "no matching overload for built-in function %s(%s)", bf->name, buf);
        return error_type();
    }
    return R;
}

/* ---------------- user function calls ---------------- */

static Type check_call(Node *n)
{
    Node *a, **slot;
    Symbol *f, *match = 0;
    Type at[MAX_PARAMS];
    int nargs = 0, i, ok, nmatch = 0, bad = 0;
    struct builtin_fn *bf;
    char buf[256];

    for (a = n->a; a; a = a->next) {
        if (is_error(check_expr(a)))
            bad = 1;
        if (nargs < MAX_PARAMS)
            at[nargs] = a->type;
        nargs++;
    }

    if (strcmp(n->sval, "<expression>") == 0) {
        sem_error(n->line, "called object is not a function");
        return error_type();
    }
    if (sym_lookup(n->sval) && !func_first(n->sval)) {
        sem_error(n->line, "'%s' is a variable, not a function", n->sval);
        return error_type();
    }
    if (bad)
        return error_type();

    f = func_first(n->sval);
    if (f == 0) {
        bf = find_builtin(n->sval);
        if (bf)
            return check_builtin(n, bf);
        sem_error(n->line, "call to undeclared function '%s'", n->sval);
        return error_type();
    }

    /* 1. exact match */
    match = func_find_exact(n->sval, nargs, at);
    /* 2. match with implicit conversions; must be unique */
    if (!match) {
        for (; f; f = func_next(f)) {
            if (f->nparams != nargs)
                continue;
            ok = 1;
            for (i = 0; i < nargs; i++) {
                if (f->pquals[i] & (Q_OUT | Q_INOUT)) {
                    if (!type_equal(at[i], f->ptypes[i])) ok = 0;
                } else if (!can_convert(at[i], f->ptypes[i])) {
                    ok = 0;
                }
            }
            if (ok) {
                match = f;
                nmatch++;
            }
        }
        if (nmatch > 1) {
            sem_error(n->line, "ambiguous call to overloaded function '%s'", n->sval);
            return error_type();
        }
    }
    if (!match) {
        bf = find_builtin(n->sval);
        if (bf)
            return check_builtin(n, bf);
        buf[0] = '\0';
        for (i = 0; i < nargs && i < MAX_PARAMS; i++)
            sprintf(buf + strlen(buf), "%s%s", i ? ", " : "", type_str(at[i]));
        sem_error(n->line, "no matching function for call to %s(%s)", n->sval, buf);
        return error_type();
    }

    for (i = 0, slot = &n->a; *slot; slot = &(*slot)->next, i++) {
        if (match->pquals[i] & (Q_OUT | Q_INOUT))
            check_lvalue(*slot, "pass as an 'out' argument");
        else
            coerce(slot, match->ptypes[i]);
    }
    n->sym = match;
    if (n_called < MAX_CALLS) {
        called[n_called].f = match;
        called[n_called].line = n->line;
        n_called++;
    }
    return match->type;
}

/* ---------------- expressions ---------------- */

static Type check_expr_inner(Node *n)
{
    Symbol *s;
    Type t;

    switch (n->kind) {
    case N_ID:
        s = sym_lookup(n->sval);
        if (s == 0) {
            if (func_first(n->sval) || find_builtin(n->sval))
                sem_error(n->line, "function '%s' used as a variable", n->sval);
            else
                sem_error(n->line, "undeclared identifier '%s'", n->sval);
            return error_type();
        }
        n->sym = s;
        n->lvalue = 1;
        return s->type;
    case N_INTLIT:
        return mk_type(TY_INT, 1, 0);
    case N_UINTLIT:
        return mk_type(TY_UINT, 1, 0);
    case N_FLOATLIT:
        if (strstr(n->sval, "lf") || strstr(n->sval, "LF"))
            return mk_type(TY_DOUBLE, 1, 0);
        return mk_type(TY_FLOAT, 1, 0);
    case N_BOOLLIT:
        return mk_type(TY_BOOL, 1, 0);
    case N_BINARY:
        return check_binary(n);
    case N_UNARY:
        return check_unary(n);
    case N_POSTFIX:
        t = check_expr(n->a);
        if (is_error(t))
            return t;
        if (!is_numeric(t)) {
            sem_error(n->line, "'%s' needs a numeric operand, got %s", op_str(n->op), type_str(t));
            return error_type();
        }
        check_lvalue(n->a, n->op == OP_INC ? "increment" : "decrement");
        return t;
    case N_ASSIGN:
        return check_assign(n);
    case N_TERNARY: {
        Type c = check_expr(n->a);
        Type x = check_expr(n->b);
        Type y = check_expr(n->c);
        if (!is_error(c) && !is_bool(c))
            sem_error(n->line, "condition of '?:' must be bool, got %s", type_str(c));
        if (is_error(x) || is_error(y))
            return error_type();
        if (type_equal(x, y))
            return x;
        if (can_convert(x, y)) { coerce(&n->b, y); return y; }
        if (can_convert(y, x)) { coerce(&n->c, x); return x; }
        sem_error(n->line, "the two branches of '?:' have different types: %s and %s",
                  type_str(x), type_str(y));
        return error_type();
    }
    case N_INDEX:
        return check_index(n);
    case N_FIELD:
        return check_field(n);
    case N_CALL:
        return check_call(n);
    case N_METHOD:
        t = check_expr(n->a);
        if (is_error(t))
            return t;
        if (strcmp(n->sval, "length") == 0 && n->b == 0 && is_array(t)) {
            if (t.arr < 0)
                return mk_type(TY_INT, 1, 0);
            return mk_type(TY_INT, 1, 0);
        }
        if (strcmp(n->sval, "length") == 0 && n->b == 0 && (is_vector(t) || is_matrix(t)))
            return mk_type(TY_INT, 1, 0);
        sem_error(n->line, "unsupported method '.%s()' on %s", n->sval, type_str(t));
        return error_type();
    case N_CTOR:
        return check_ctor(n);
    case N_COMMA:
        check_expr(n->a);
        return check_expr(n->b);
    case N_CONV:
        return n->type;
    case N_INITLIST:
        sem_error(n->line, "initializer list used as an expression");
        return error_type();
    default:
        sem_error(n->line, "unexpected node in expression");
        return error_type();
    }
}

static Type check_expr(Node *n)
{
    n->type = check_expr_inner(n);
    return n->type;
}

/* ---------------- declarations ---------------- */

/* check an initializer against the declared type; may fill in an unsized array */
static void check_init(Node **slot, Type *t, int line)
{
    Node *init = *slot, **p;
    StructDef *sd;
    Type et;
    int n = 0, i;

    if (init->kind == N_INITLIST) {
        init->type = *t;
        if (is_array(*t)) {
            et = elem_type(*t);
            for (p = &init->a; *p; p = &(*p)->next, n++)
                check_init(p, &et, line);
            if (t->arr < 0)
                t->arr = n;
            else if (n != t->arr)
                sem_error(line, "initializer list has %d element(s), array has %d", n, t->arr);
        } else if (t->base == TY_STRUCT) {
            sd = struct_lookup(t->name);
            n = list_len(init->a);
            if (sd && n != sd->nmembers) {
                sem_error(line, "initializer list has %d element(s), struct %s has %d member(s)",
                          n, sd->name, sd->nmembers);
                return;
            }
            for (i = 0, p = &init->a; sd && *p; p = &(*p)->next, i++)
                check_init(p, &sd->mtypes[i], line);
        } else if (is_vector(*t) || is_matrix(*t)) {
            /* { a, b, c } for a vector: each element is one component */
            et = is_matrix(*t) ? mk_type(t->base, t->vec, 0) : mk_type(t->base, 1, 0);
            for (p = &init->a; *p; p = &(*p)->next, n++)
                check_init(p, &et, line);
            if (n != (is_matrix(*t) ? t->cols : t->vec))
                sem_error(line, "initializer list has %d element(s) for %s", n, type_str(*t));
        } else {
            sem_error(line, "initializer list used for a scalar of type %s", type_str(*t));
        }
        return;
    }

    if (is_error(check_expr(init)))
        return;
    if (t->arr < 0 && is_array(init->type) && type_equal(elem_type(*t), elem_type(init->type)))
        t->arr = init->type.arr;
    if (!coerce(slot, *t))
        sem_error(line, "cannot initialize a variable of type %s with a value of type %s",
                  type_str(*t), type_str(init->type));
}

static void check_struct_def(Node *s);

static void declare_var(Node *v, int global)
{
    Type t;
    Symbol *prev, *s;
    long val;
    int storage = v->quals & (Q_IN | Q_OUT | Q_INOUT | Q_UNIFORM | Q_BUFFER | Q_SHARED);

    if (v->kind == N_STRUCTDEF) {
        check_struct_def(v);
        return;
    }
    if (v->kind != N_VARDECL)
        return;

    t = resolve_type(v->type, v->d, v->line);
    if (is_void(t)) {
        sem_error(v->line, "variable '%s' declared void", v->sval);
        t = error_type();
    }

    if (!global && storage)
        sem_error(v->line, "storage qualifier '%s' is not allowed on local variable '%s'",
                  quals_str(storage), v->sval);
    if (global && (v->quals & Q_INOUT))
        sem_error(v->line, "'inout' is only allowed on function parameters");
    if ((t.base == TY_SAMPLER || t.base == TY_IMAGE) && !(v->quals & Q_UNIFORM))
        sem_error(v->line, "%s '%s' must be declared uniform", type_str(t), v->sval);
    if ((v->quals & (Q_IN | Q_OUT)) && global && (t.base == TY_BOOL))
        sem_error(v->line, "shader inputs/outputs cannot be of type %s", type_str(t));
    if ((v->quals & Q_CONST) && v->a == 0)
        sem_error(v->line, "const variable '%s' must be initialized", v->sval);
    if (v->a && (v->quals & (Q_IN | Q_OUT)))
        sem_error(v->line, "shader input/output '%s' cannot have an initializer", v->sval);

    if (v->a && !is_error(t))
        check_init(&v->a, &t, v->line);
    if (t.arr < 0 && !global)
        sem_error(v->line, "array '%s' has no size", v->sval);

    prev = sym_lookup_current(v->sval);
    if (prev) {
        sem_error(v->line, "redeclaration of '%s' (previously declared on line %d)",
                  v->sval, prev->line);
        return;
    }
    if (global && func_first(v->sval)) {
        sem_error(v->line, "'%s' is already declared as a function", v->sval);
        return;
    }
    if (strncmp(v->sval, "gl_", 3) == 0)
        sem_error(v->line, "identifier '%s' is reserved (names starting with gl_)", v->sval);

    s = sym_insert(v->sval, SYM_VAR, t, v->quals, v->line);
    if ((v->quals & Q_CONST) && v->a && is_integer(t) && is_scalar(t) && const_eval(v->a, &val)) {
        s->has_const = 1;
        s->const_val = val;
    }
    v->type = t;
    v->sym = s;
}

/* fill a StructDef from a list of member declarations */
static void collect_members(StructDef *sd, Node *members, char *owner)
{
    Node *m;
    Type t;
    int i;

    for (m = members; m; m = m->next) {
        if (m->kind == N_STRUCTDEF) {      /* nested struct definition */
            check_struct_def(m);
            continue;
        }
        t = resolve_type(m->type, m->d, m->line);
        if (is_void(t))
            sem_error(m->line, "member '%s' of %s declared void", m->sval, owner);
        if (m->quals & (Q_IN | Q_OUT | Q_INOUT | Q_UNIFORM | Q_CONST))
            sem_error(m->line, "member '%s' of %s cannot have storage qualifiers", m->sval, owner);
        for (i = 0; i < sd->nmembers; i++)
            if (strcmp(sd->mnames[i], m->sval) == 0)
                sem_error(m->line, "duplicate member '%s' in %s", m->sval, owner);
        m->type = t;
        if (sd->nmembers < MAX_MEMBERS) {
            sd->mnames[sd->nmembers] = m->sval;
            sd->mtypes[sd->nmembers] = t;
            sd->nmembers++;
        }
    }
}

static void check_struct_def(Node *s)
{
    StructDef *sd;
    if (struct_lookup(s->sval)) {
        sem_error(s->line, "redefinition of struct '%s'", s->sval);
        return;
    }
    sd = struct_define(s->sval, s->line);
    collect_members(sd, s->a, s->sval);
    if (sd->nmembers == 0)
        sem_error(s->line, "struct '%s' has no members", s->sval);
}

static void check_block(Node *b)
{
    StructDef *sd;
    Node *m;
    Type t;
    char *name;

    if (!(b->quals & (Q_UNIFORM | Q_BUFFER | Q_IN | Q_OUT)))
        sem_error(b->line, "interface block '%s' needs uniform, buffer, in or out", b->sval);

    if (b->b) {                         /* block with an instance name: a struct-like type */
        name = (char *)malloc(strlen(b->sval) + 8);
        sprintf(name, "%s", b->sval);
        if (struct_lookup(name)) {
            sem_error(b->line, "redefinition of block '%s'", name);
            return;
        }
        sd = struct_define(name, b->line);
        collect_members(sd, b->a, name);
        b->b->type = type_from_name(name);
        b->b->quals = b->quals;
        declare_var(b->b, 1);
        return;
    }

    /* no instance name: members become global variables */
    sd = (StructDef *)calloc(1, sizeof(StructDef));
    collect_members(sd, b->a, b->sval);
    for (m = b->a; m; m = m->next) {
        if (m->kind != N_VARDECL)
            continue;
        t = m->type;
        if (sym_lookup_current(m->sval)) {
            sem_error(m->line, "redeclaration of '%s'", m->sval);
            continue;
        }
        m->sym = sym_insert(m->sval, SYM_VAR, t, b->quals, m->line);
    }
}

/* ---------------- statements ---------------- */

static int always_returns(Node *s)
{
    Node *p;
    if (s == 0)
        return 0;
    switch (s->kind) {
    case N_RETURN: case N_DISCARD:
        return 1;
    case N_COMPOUND:
        for (p = s->a; p; p = p->next)
            if (always_returns(p))
                return 1;
        return 0;
    case N_IF:
        return s->c && always_returns(s->b) && always_returns(s->c);
    case N_DOWHILE:
        return always_returns(s->a);
    default:
        return 0;
    }
}

static void check_condition(Node *e, char *what)
{
    Type t = check_expr(e);
    if (!is_error(t) && !is_bool(t))
        sem_error(e->line, "condition of '%s' must be a bool scalar, got %s", what, type_str(t));
}

static void check_stmt(Node *n)
{
    Node *p;
    Type t;
    long v;
    int i, sd;

    if (n == 0)
        return;
    switch (n->kind) {
    case N_COMPOUND:
        scope_push();
        check_stmt_list(n->a);
        scope_pop();
        break;
    case N_DECLSTMT:
        for (p = n->a; p; p = p->next) {
            if (p->kind == N_VARDECL || p->kind == N_STRUCTDEF)
                declare_var(p, 0);
            else if (p->kind == N_BLOCKDECL)
                sem_error(p->line, "interface blocks must be declared at global scope");
        }
        break;
    case N_EXPRSTMT:
        if (n->a)
            check_expr(n->a);
        break;
    case N_IF:
        check_condition(n->a, "if");
        check_stmt(n->b);
        check_stmt(n->c);
        break;
    case N_WHILE:
        check_condition(n->a, "while");
        loop_depth++;
        check_stmt(n->b);
        loop_depth--;
        break;
    case N_DOWHILE:
        loop_depth++;
        check_stmt(n->a);
        loop_depth--;
        check_condition(n->b, "do-while");
        break;
    case N_FOR:
        scope_push();
        check_stmt(n->a);
        if (n->b)
            check_condition(n->b, "for");
        if (n->c)
            check_expr(n->c);
        loop_depth++;
        check_stmt(n->d);
        loop_depth--;
        scope_pop();
        break;
    case N_SWITCH:
        t = check_expr(n->a);
        if (!is_error(t) && !(is_integer(t) && is_scalar(t)))
            sem_error(n->line, "switch expression must be an int or uint scalar, got %s", type_str(t));
        if (switch_depth < 16) {
            n_case_vals[switch_depth] = 0;
            has_default[switch_depth] = 0;
        }
        switch_depth++;
        check_stmt(n->b);
        switch_depth--;
        break;
    case N_CASE:
        t = check_expr(n->a);
        if (switch_depth == 0) {
            sem_error(n->line, "'case' label outside a switch statement");
            break;
        }
        if (is_error(t))
            break;
        if (!is_integer(t) || !is_scalar(t) || !const_eval(n->a, &v)) {
            sem_error(n->line, "case label must be a constant integer expression");
            break;
        }
        sd = switch_depth - 1;
        if (sd < 16) {
            for (i = 0; i < n_case_vals[sd]; i++)
                if (case_vals[sd][i] == v)
                    sem_error(n->line, "duplicate case value %ld", v);
            if (n_case_vals[sd] < MAX_CASES)
                case_vals[sd][n_case_vals[sd]++] = v;
        }
        break;
    case N_DEFAULT:
        if (switch_depth == 0)
            sem_error(n->line, "'default' label outside a switch statement");
        else if (switch_depth <= 16 && has_default[switch_depth - 1]++)
            sem_error(n->line, "multiple default labels in one switch");
        break;
    case N_BREAK:
        if (loop_depth == 0 && switch_depth == 0)
            sem_error(n->line, "'break' outside a loop or switch");
        break;
    case N_CONTINUE:
        if (loop_depth == 0)
            sem_error(n->line, "'continue' outside a loop");
        break;
    case N_RETURN:
        if (cur_func == 0)
            break;
        if (n->a == 0) {
            if (!is_void(cur_func->type))
                sem_error(n->line, "function '%s' must return a value of type %s",
                          cur_func->name, type_str(cur_func->type));
            break;
        }
        t = check_expr(n->a);
        if (is_void(cur_func->type)) {
            sem_error(n->line, "void function '%s' cannot return a value", cur_func->name);
            break;
        }
        if (!is_error(t) && !coerce(&n->a, cur_func->type))
            sem_error(n->line, "returning %s from function '%s' declared to return %s",
                      type_str(t), cur_func->name, type_str(cur_func->type));
        break;
    case N_DISCARD:
        break;
    case N_PRECISION: case N_IGNORED:
        break;
    default:
        sem_error(n->line, "unexpected statement");
    }
}

static void check_stmt_list(Node *list)
{
    for (; list; list = list->next)
        check_stmt(list);
}

/* ---------------- functions ---------------- */

static void check_function(Node *f)
{
    Node *p;
    Type ret, pt[MAX_PARAMS];
    int pq[MAX_PARAMS], np = 0, i;
    Symbol *sym, *prev, *s;

    ret = resolve_type(f->type, 0, f->line);
    if (f->quals & (Q_IN | Q_OUT | Q_INOUT | Q_UNIFORM))
        sem_error(f->line, "function '%s' cannot have a storage qualifier on its return type", f->sval);

    /* f(void) means no parameters */
    if (f->a && f->a->next == 0 && f->a->sval == 0 && is_void(f->a->type) && f->a->d == 0)
        f->a = 0;

    for (p = f->a; p; p = p->next) {
        p->type = resolve_type(p->type, p->d, p->line);
        if (is_void(p->type))
            sem_error(p->line, "parameter %d of '%s' declared void", np + 1, f->sval);
        if (p->quals & (Q_UNIFORM | Q_BUFFER | Q_SHARED))
            sem_error(p->line, "parameter %s of '%s' cannot be uniform/buffer/shared",
                      p->sval ? p->sval : "", f->sval);
        if (np < MAX_PARAMS) {
            pt[np] = p->type;
            pq[np] = p->quals & (Q_OUT | Q_INOUT) ? p->quals : (p->quals | Q_IN);
        }
        np++;
    }
    if (np > MAX_PARAMS) {
        sem_error(f->line, "too many parameters in '%s'", f->sval);
        np = MAX_PARAMS;
    }

    s = sym_lookup(f->sval);
    if (s && s->depth == 0 && s->kind != SYM_FUNC) {
        sem_error(f->line, "'%s' is already declared as a variable", f->sval);
        return;
    }

    prev = func_find_exact(f->sval, np, pt);
    if (prev) {
        if (!type_equal(prev->type, ret))
            sem_error(f->line, "conflicting return type for '%s' (was %s on line %d)",
                      f->sval, type_str(prev->type), prev->line);
        if (prev->defined && f->b)
            sem_error(f->line, "redefinition of function '%s' (first defined on line %d)",
                      f->sval, prev->line);
        sym = prev;
    } else {
        sym = func_insert(f->sval, ret, np, pt, pq, f->line);
    }
    f->sym = sym;

    if (strcmp(f->sval, "main") == 0 && (!is_void(ret) || np != 0))
        sem_error(f->line, "main() must be declared 'void main()'");

    if (f->b == 0)
        return;
    sym->defined = 1;
    sym->line = f->line;

    cur_func = sym;
    set_scope_name(f->sval);
    scope_push();
    for (p = f->a, i = 0; p; p = p->next, i++) {
        if (p->sval == 0)
            continue;
        if (sym_lookup_current(p->sval)) {
            sem_error(p->line, "duplicate parameter name '%s'", p->sval);
            continue;
        }
        p->sym = sym_insert(p->sval, SYM_PARAM, p->type, p->quals, p->line);
    }
    /* the body shares the parameters' scope */
    check_stmt_list(f->b->a);
    if (!is_void(ret) && !always_returns(f->b))
        sem_warning(f->line, "function '%s' may reach its end without returning a value", f->sval);
    scope_pop();
    set_scope_name("global");
    cur_func = 0;
}

/* ---------------- built-in variables ---------------- */

static void add_builtin(char *name, Type t, int readonly)
{
    Symbol *s = sym_insert(name, SYM_BUILTIN, t, 0, 0);
    s->readonly = readonly;
}

static void add_builtins(void)
{
    add_builtin("gl_Position",  mk_type(TY_FLOAT, 4, 0), 0);
    add_builtin("gl_PointSize", mk_type(TY_FLOAT, 1, 0), 0);
    add_builtin("gl_VertexID",  mk_type(TY_INT, 1, 0), 1);
    add_builtin("gl_InstanceID", mk_type(TY_INT, 1, 0), 1);
    add_builtin("gl_FragCoord", mk_type(TY_FLOAT, 4, 0), 1);
    add_builtin("gl_FrontFacing", mk_type(TY_BOOL, 1, 0), 1);
    add_builtin("gl_PointCoord", mk_type(TY_FLOAT, 2, 0), 1);
    add_builtin("gl_FragDepth", mk_type(TY_FLOAT, 1, 0), 0);
    add_builtin("gl_FragColor", mk_type(TY_FLOAT, 4, 0), 0);
    add_builtin("gl_LocalInvocationID", mk_type(TY_UINT, 3, 0), 1);
    add_builtin("gl_GlobalInvocationID", mk_type(TY_UINT, 3, 0), 1);
    add_builtin("gl_WorkGroupID", mk_type(TY_UINT, 3, 0), 1);
    add_builtin("gl_LocalInvocationIndex", mk_type(TY_UINT, 1, 0), 1);
}

/* ---------------- entry point ---------------- */

void semantic_analyze(Node *program)
{
    Node *e;
    Symbol *m;
    int i;

    symtab_init();
    add_builtins();

    for (e = program ? program->a : 0; e; e = e->next) {
        switch (e->kind) {
        case N_FUNC:
            check_function(e);
            break;
        case N_VARDECL: case N_STRUCTDEF:
            declare_var(e, 1);
            break;
        case N_BLOCKDECL:
            check_block(e);
            break;
        case N_PRECISION:
            if (!is_scalar(e->type) || (e->type.base != TY_FLOAT && e->type.base != TY_INT))
                if (e->type.base != TY_SAMPLER)
                    sem_error(e->line, "precision can only be set for float, int and sampler types");
            break;
        default:
            break;
        }
    }

    m = func_first("main");
    if (m == 0 || !m->defined)
        sem_error(0, "no definition of 'void main()' found");

    /* a function that is called must have a body somewhere */
    for (i = 0; i < n_called; i++)
        if (!called[i].f->defined)
            sem_error(called[i].line, "function '%s' is called but never defined",
                      called[i].f->name);

    func_mangle_overloads();
    ast_typed = 1;
}
