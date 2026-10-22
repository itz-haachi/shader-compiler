/*
 * c_codegen.c -- Phase 7 standalone C source generation.
 *
 * The generated C program uses one small value type (MGValue) for every GLSL
 * scalar, vector, matrix and array value.  This keeps the backend independent
 * of a particular shader ABI while still producing ordinary, compilable C99.
 * The runtime is intentionally educational: vector arithmetic is element-wise
 * and texture sampling is represented by a deterministic zero value.
 */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_codegen.h"
#include "optimizer.h"
#include "symtab.h"

static char **source_lines;
static int n_source_lines;
static int cap_source_lines;

static char *dup_cgen(const char *s)
{
    size_t n = strlen(s ? s : "");
    char *copy = (char *)malloc(n + 1);
    if (!copy)
        return 0;
    memcpy(copy, s ? s : "", n + 1);
    return copy;
}

static void cgen_line(const char *text)
{
    char **grown;
    if (n_source_lines == cap_source_lines) {
        cap_source_lines = cap_source_lines ? cap_source_lines * 2 : 256;
        grown = (char **)realloc(source_lines,
                                 (size_t)cap_source_lines * sizeof(*source_lines));
        if (!grown)
            return;
        source_lines = grown;
    }
    source_lines[n_source_lines++] = dup_cgen(text);
}

static void cgen_linef(const char *format, ...)
{
    char buffer[4096];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);
    cgen_line(buffer);
}

static void cgen_append_last(const char *suffix)
{
    size_t old_len, add_len;
    char *grown;
    if (n_source_lines == 0 || !suffix)
        return;
    old_len = strlen(source_lines[n_source_lines - 1]);
    add_len = strlen(suffix);
    grown = (char *)realloc(source_lines[n_source_lines - 1], old_len + add_len + 1);
    if (!grown)
        return;
    memcpy(grown + old_len, suffix, add_len + 1);
    source_lines[n_source_lines - 1] = grown;
}

static void cgen_runtime_indented(const char *text, int indent)
{
    char prefix[128];
    int spaces = indent * 4;
    while (text && isspace((unsigned char)*text))
        text++;
    if (spaces < 0)
        spaces = 0;
    if (spaces > (int)sizeof(prefix) - 1)
        spaces = (int)sizeof(prefix) - 1;
    memset(prefix, ' ', (size_t)spaces);
    prefix[spaces] = '\0';
    cgen_linef("%s%s", prefix, text ? text : "");
}

/* Split a compact runtime helper at C statement and brace boundaries. */
static void cgen_runtime_text(const char *text)
{
    char token[4096];
    int length = 0;
    int indent = 0;
    int paren_depth = 0;
    int in_string = 0;
    int escaped = 0;
    int closed_brace = 0;
    size_t i;

    if (!text || !*text) {
        cgen_line("");
        return;
    }
    for (i = 0; text[i]; i++) {
        char ch = text[i];
        if (in_string) {
            if (length < (int)sizeof(token) - 1)
                token[length++] = ch;
            if (ch == '"' && !escaped)
                in_string = 0;
            escaped = ch == '\\' && !escaped;
            continue;
        }
        if (ch == '"') {
            in_string = 1;
            if (length < (int)sizeof(token) - 1)
                token[length++] = ch;
        } else if (ch == '(') {
            paren_depth++;
            if (length < (int)sizeof(token) - 1)
                token[length++] = ch;
        } else if (ch == ')') {
            if (paren_depth > 0)
                paren_depth--;
            if (length < (int)sizeof(token) - 1)
                token[length++] = ch;
        } else if (ch == '{') {
            while (length > 0 && isspace((unsigned char)token[length - 1]))
                length--;
            token[length++] = ' ';
            token[length++] = '{';
            token[length] = '\0';
            cgen_runtime_indented(token, indent);
            length = 0;
            indent++;
            closed_brace = 0;
        } else if (ch == '}') {
            while (length > 0 && isspace((unsigned char)token[length - 1]))
                length--;
            token[length] = '\0';
            if (length > 0) {
                cgen_runtime_indented(token, indent);
                length = 0;
            }
            if (indent > 0)
                indent--;
            cgen_runtime_indented("}", indent);
            closed_brace = 1;
        } else if (ch == ';' && paren_depth == 0) {
            if (length == 0 && closed_brace) {
                cgen_append_last(";");
                closed_brace = 0;
                continue;
            }
            if (length < (int)sizeof(token) - 1)
                token[length++] = ch;
            token[length] = '\0';
            while (length > 0 && isspace((unsigned char)token[length - 1]))
                token[--length] = '\0';
            if (length > 0)
                cgen_runtime_indented(token, indent);
            length = 0;
            closed_brace = 0;
        } else if (length < (int)sizeof(token) - 1) {
            token[length++] = ch;
            closed_brace = 0;
        }
    }
    while (length > 0 && isspace((unsigned char)token[length - 1]))
        token[--length] = '\0';
    token[length] = '\0';
    if (length > 0)
        cgen_runtime_indented(token, indent);
}

void c_codegen_free(void)
{
    int i;
    for (i = 0; i < n_source_lines; i++)
        free(source_lines[i]);
    free(source_lines);
    source_lines = 0;
    n_source_lines = 0;
    cap_source_lines = 0;
}

static int is_temp_name(const char *name)
{
    int i;
    if (!name || name[0] != 't' || !name[1])
        return 0;
    for (i = 1; name[i]; i++)
        if (!isdigit((unsigned char)name[i]))
            return 0;
    return 1;
}

static int is_integer_literal(const char *name)
{
    char *end;
    if (!name || !*name)
        return 0;
    errno = 0;
    (void)strtoll(name, &end, 0);
    if (end == name)
        return 0;
    if (*end == 'u' || *end == 'U')
        end++;
    return *end == '\0';
}

static int is_float_literal(const char *name)
{
    char *end;
    if (!name || !*name)
        return 0;
    errno = 0;
    (void)strtod(name, &end);
    if (end == name)
        return 0;
    if (*end == 'f' || *end == 'F' || *end == 'l' || *end == 'L')
        end++;
    return *end == '\0';
}

static int is_literal(const char *name)
{
    return name && (!strcmp(name, "true") || !strcmp(name, "false") ||
                    is_integer_literal(name) || is_float_literal(name));
}

static const char *c_identifier(const char *name)
{
    static char buffers[16][256];
    static int next_buffer;
    char *out;
    int i, n = 0;

    next_buffer = (next_buffer + 1) % 16;
    out = buffers[next_buffer];
    if (!name || !*name) {
        strcpy(out, "v_empty");
        return out;
    }
    if (is_temp_name(name)) {
        sprintf(out, "tmp%s", name + 1);
        return out;
    }
    strcpy(out, "v_");
    n = 2;
    for (i = 0; name[i] && n < 245; i++) {
        unsigned char ch = (unsigned char)name[i];
        out[n++] = (char)(isalnum(ch) || ch == '_' ? ch : '_');
    }
    out[n] = '\0';
    return out;
}

static const char *c_function_name(const char *name)
{
    static char buffers[8][256];
    static int next_buffer;
    char *out;
    int i, n = 0;

    next_buffer = (next_buffer + 1) % 8;
    out = buffers[next_buffer];
    if (!name)
        name = "unknown";
    strcpy(out, "shader_");
    n = 7;
    for (i = 0; name[i] && n < 245; i++) {
        unsigned char ch = (unsigned char)name[i];
        out[n++] = (char)(isalnum(ch) || ch == '_' ? ch : '_');
    }
    out[n] = '\0';
    return out;
}

static const char *c_label_name(const char *name)
{
    static char buffers[8][128];
    static int next_buffer;
    char *out;
    int i, n = 0;

    next_buffer = (next_buffer + 1) % 8;
    out = buffers[next_buffer];
    strcpy(out, "label_");
    n = 6;
    for (i = 0; name && name[i] && n < 120; i++) {
        unsigned char ch = (unsigned char)name[i];
        out[n++] = (char)(isalnum(ch) || ch == '_' ? ch : '_');
    }
    out[n] = '\0';
    return out;
}

static const char *base_name(const char *name)
{
    static char buffer[256];
    const char *dot;
    size_t n;
    if (!name)
        return "";
    dot = strchr(name, '.');
    if (!dot)
        return name;
    n = (size_t)(dot - name);
    if (n >= sizeof(buffer))
        n = sizeof(buffer) - 1;
    memcpy(buffer, name, n);
    buffer[n] = '\0';
    return buffer;
}

static const char *value_expr(const char *name)
{
    static char buffers[32][512];
    static int next_buffer;
    const char *dot;
    const char *base;
    const char *member;
    char *out;

    next_buffer = (next_buffer + 1) % 32;
    out = buffers[next_buffer];
    if (!name || !*name) {
        strcpy(out, "mg_zero()");
    } else if (!strcmp(name, "true")) {
        strcpy(out, "mg_bool(1)");
    } else if (!strcmp(name, "false")) {
        strcpy(out, "mg_bool(0)");
    } else if (is_integer_literal(name)) {
        if (name[strlen(name) - 1] == 'u' || name[strlen(name) - 1] == 'U')
            sprintf(out, "mg_uint((uint64_t)(%s))", name);
        else
            sprintf(out, "mg_int((int64_t)(%s))", name);
    } else if (is_float_literal(name)) {
        sprintf(out, "mg_float((double)(%s))", name);
    } else if ((dot = strchr(name, '.')) != 0) {
        base = base_name(name);
        member = dot + 1;
        sprintf(out, "mg_member(%s, \"%s\")", value_expr(base), member);
    } else {
        strcpy(out, c_identifier(name));
    }
    return out;
}

static int declaration_exists(char **names, int count, const char *name)
{
    int i;
    for (i = 0; i < count; i++)
        if (!strcmp(names[i], name))
            return 1;
    return 0;
}

static void add_declaration(char **names, int *count, int max, const char *name)
{
    const char *base;
    char normalized[256];
    if (!name || !*name || is_literal(name))
        return;
    base = base_name(name);
    if (!strcmp(base, "L0") || (base[0] == 'L' && isdigit((unsigned char)base[1])))
        return;
    strncpy(normalized, base, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    if (is_temp_name(normalized)) {
        /* temporaries are always function-local */
    } else {
        Symbol *global = sym_lookup_ir_scope(normalized, "global");
        if (global && global->depth == 0)
            return;
    }
    if (*count < max && !declaration_exists(names, *count, normalized))
        names[(*count)++] = dup_cgen(normalized);
}

static void add_quad_declarations(Quad *q, char **names, int *count, int max)
{
    const char *op = q->op;
    if (!strcmp(op, "func") || !strcmp(op, "endfunc") || !strcmp(op, "label") ||
        !strcmp(op, "goto"))
        return;
    if (!strcmp(op, "call")) {
        add_declaration(names, count, max, q->result);
        return;
    }
    if (!strcmp(op, "construct")) {
        add_declaration(names, count, max, q->result);
        return;
    }
    if (!strcmp(op, "conv")) {
        add_declaration(names, count, max, q->arg1);
        add_declaration(names, count, max, q->result);
        return;
    }
    if (!strcmp(op, "swizzle") || !strcmp(op, "member")) {
        add_declaration(names, count, max, q->arg1);
        add_declaration(names, count, max, q->result);
        return;
    }
    if (!strcmp(op, "swizzle=") || !strcmp(op, "member=")) {
        add_declaration(names, count, max, q->arg1);
        add_declaration(names, count, max, q->result);
        return;
    }
    if (!strcmp(op, "[]")) {
        add_declaration(names, count, max, q->arg1);
        add_declaration(names, count, max, q->arg2);
        add_declaration(names, count, max, q->result);
        return;
    }
    if (!strcmp(op, "[]=")) {
        add_declaration(names, count, max, q->arg1);
        add_declaration(names, count, max, q->arg2);
        add_declaration(names, count, max, q->result);
        return;
    }
    add_declaration(names, count, max, q->arg1);
    add_declaration(names, count, max, q->arg2);
    add_declaration(names, count, max, q->result);
}

static void free_declarations(char **names, int count)
{
    int i;
    for (i = 0; i < count; i++)
        free(names[i]);
}

static void emit_runtime(void)
{
    static const char *runtime[] = {
        "#include <math.h>",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include <stdio.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "",
        "typedef struct MGValue MGValue;",
        "struct MGValue { int kind; int n; double f[4]; int64_t i; uint64_t u; int b; MGValue *items; size_t count; };",
        "enum { MG_INT = 1, MG_UINT, MG_FLOAT, MG_BOOL, MG_VECTOR, MG_ARRAY };",
        "static int mg_discarded;",
        "static MGValue mg_zero(void) { MGValue v; memset(&v, 0, sizeof(v)); v.kind = MG_FLOAT; v.n = 1; return v; }",
        "static MGValue mg_int(int64_t x) { MGValue v = mg_zero(); v.kind = MG_INT; v.i = x; v.f[0] = (double)x; return v; }",
        "static MGValue mg_uint(uint64_t x) { MGValue v = mg_zero(); v.kind = MG_UINT; v.u = x; v.f[0] = (double)x; return v; }",
        "static MGValue mg_float(double x) { MGValue v = mg_zero(); v.kind = MG_FLOAT; v.f[0] = x; return v; }",
        "static MGValue mg_bool(int x) { MGValue v = mg_zero(); v.kind = MG_BOOL; v.b = !!x; v.f[0] = (double)v.b; return v; }",
        "static double mg_component(MGValue v, int i) { if (v.kind == MG_VECTOR && i < v.n) return v.f[i]; return v.f[0]; }",
        "static double mg_number(MGValue v) { return mg_component(v, 0); }",
        "static int mg_truth(MGValue v) { return mg_number(v) != 0.0; }",
        "static MGValue mg_arith(MGValue a, MGValue b, int op) { MGValue v = mg_zero(); int i, n = a.n > b.n ? a.n : b.n; v.n = n; v.kind = n > 1 ? MG_VECTOR : MG_FLOAT; for (i = 0; i < n && i < 4; i++) { double x = mg_component(a, i), y = mg_component(b, i); if (op == 0) v.f[i] = x + y; else if (op == 1) v.f[i] = x - y; else if (op == 2) v.f[i] = x * y; else if (op == 3) v.f[i] = y == 0.0 ? 0.0 : x / y; else v.f[i] = fmod(x, y); } return v; }",
        "static MGValue mg_add(MGValue a, MGValue b) { return mg_arith(a, b, 0); }",
        "static MGValue mg_sub(MGValue a, MGValue b) { return mg_arith(a, b, 1); }",
        "static MGValue mg_mul(MGValue a, MGValue b) { return mg_arith(a, b, 2); }",
        "static MGValue mg_div(MGValue a, MGValue b) { return mg_arith(a, b, 3); }",
        "static MGValue mg_mod(MGValue a, MGValue b) { return mg_arith(a, b, 4); }",
        "static MGValue mg_extrema(MGValue a, MGValue b, int want_max) { MGValue v = mg_zero(); int i, n = a.n > b.n ? a.n : b.n; v.n = n; v.kind = n > 1 ? MG_VECTOR : MG_FLOAT; for (i = 0; i < n && i < 4; i++) { double x = mg_component(a, i), y = mg_component(b, i); v.f[i] = want_max ? (x > y ? x : y) : (x < y ? x : y); } return v; }",
        "static MGValue mg_maxv(MGValue a, MGValue b) { return mg_extrema(a, b, 1); }",
        "static MGValue mg_minv(MGValue a, MGValue b) { return mg_extrema(a, b, 0); }",
        "static MGValue mg_clampv(MGValue x, MGValue lo, MGValue hi) { return mg_minv(mg_maxv(x, lo), hi); }",
        "static MGValue mg_mixv(MGValue a, MGValue b, MGValue t) { return mg_add(mg_mul(a, mg_sub(mg_float(1.0), t)), mg_mul(b, t)); }",
        "static MGValue mg_absv(MGValue a) { MGValue v = a; int i; for (i = 0; i < a.n && i < 4; i++) v.f[i] = fabs(mg_component(a, i)); return v; }",
        "static MGValue mg_sqrtv(MGValue a) { MGValue v = a; int i; for (i = 0; i < a.n && i < 4; i++) v.f[i] = sqrt(mg_component(a, i)); return v; }",
        "static MGValue mg_bit(MGValue a, MGValue b, int op) { uint64_t x = a.kind == MG_UINT ? a.u : (uint64_t)a.i, y = b.kind == MG_UINT ? b.u : (uint64_t)b.i; if (op == 0) return mg_uint(x & y); if (op == 1) return mg_uint(x | y); if (op == 2) return mg_uint(x ^ y); if (op == 3) return mg_uint(x << y); return mg_uint(x >> y); }",
        "static MGValue mg_and(MGValue a, MGValue b) { return mg_bit(a, b, 0); }",
        "static MGValue mg_or(MGValue a, MGValue b) { return mg_bit(a, b, 1); }",
        "static MGValue mg_xor(MGValue a, MGValue b) { return mg_bit(a, b, 2); }",
        "static MGValue mg_shl(MGValue a, MGValue b) { return mg_bit(a, b, 3); }",
        "static MGValue mg_shr(MGValue a, MGValue b) { return mg_bit(a, b, 4); }",
        "static MGValue mg_cmp(MGValue a, MGValue b, int op) { double x = mg_number(a), y = mg_number(b); int r = op == 0 ? x < y : op == 1 ? x > y : op == 2 ? x <= y : op == 3 ? x >= y : op == 4 ? x == y : x != y; return mg_bool(r); }",
        "static MGValue mg_lt(MGValue a, MGValue b) { return mg_cmp(a, b, 0); }",
        "static MGValue mg_gt(MGValue a, MGValue b) { return mg_cmp(a, b, 1); }",
        "static MGValue mg_le(MGValue a, MGValue b) { return mg_cmp(a, b, 2); }",
        "static MGValue mg_ge(MGValue a, MGValue b) { return mg_cmp(a, b, 3); }",
        "static MGValue mg_eq(MGValue a, MGValue b) { return mg_cmp(a, b, 4); }",
        "static MGValue mg_ne(MGValue a, MGValue b) { return mg_cmp(a, b, 5); }",
        "static MGValue mg_land(MGValue a, MGValue b) { return mg_bool(mg_truth(a) && mg_truth(b)); }",
        "static MGValue mg_lor(MGValue a, MGValue b) { return mg_bool(mg_truth(a) || mg_truth(b)); }",
        "static MGValue mg_neg(MGValue a) { MGValue v = a; int i; for (i = 0; i < a.n && i < 4; i++) v.f[i] = -mg_component(a, i); return v; }",
        "static MGValue mg_not(MGValue a) { return mg_bool(!mg_truth(a)); }",
        "static MGValue mg_bitnot(MGValue a) { return mg_uint(~(a.kind == MG_UINT ? a.u : (uint64_t)a.i)); }",
        "static MGValue mg_conv(MGValue a, const char *type) { (void)type; return a; }",
        "static MGValue mg_make_vec(int n, int argc, const MGValue *args) { MGValue v = mg_zero(); int i, j, k = 0; v.kind = n > 1 ? MG_VECTOR : MG_FLOAT; v.n = n > 4 ? 4 : n; for (i = 0; i < argc && k < 4; i++) for (j = 0; j < args[i].n && k < 4; j++) v.f[k++] = mg_component(args[i], j); return v; }",
        "static int mg_component_index(char c) { return c == 'x' || c == 'r' ? 0 : c == 'y' || c == 'g' ? 1 : c == 'z' || c == 'b' ? 2 : 3; }",
        "static MGValue mg_swizzle(MGValue a, const char *sw) { MGValue v = mg_zero(); size_t i, n = strlen(sw); v.kind = n > 1 ? MG_VECTOR : MG_FLOAT; v.n = n > 4 ? 4 : (int)n; for (i = 0; i < n && i < 4; i++) v.f[i] = mg_component(a, mg_component_index(sw[i])); return v; }",
        "static MGValue mg_member(MGValue a, const char *member) { (void)member; return a; }",
        "static void mg_swizzle_set(MGValue *dst, const char *sw, MGValue src) { size_t i; for (i = 0; sw[i] && i < 4; i++) dst->f[mg_component_index(sw[i])] = mg_component(src, (int)i); }",
        "static void mg_member_set(MGValue *dst, const char *member, MGValue src) { (void)member; *dst = src; }",
        "static MGValue mg_loadidx(MGValue base, MGValue index) { int i = (int)mg_number(index); if (base.items && i >= 0 && (size_t)i < base.count) return base.items[i]; if (base.n > 1 && i >= 0 && i < base.n) return mg_float(base.f[i]); return mg_zero(); }",
        "static void mg_storeidx(MGValue *base, MGValue index, MGValue value) { int i = (int)mg_number(index); if (i < 0 || i >= 64) return; if (!base->items) { base->items = (MGValue *)calloc(64, sizeof(MGValue)); base->count = 64; base->kind = MG_ARRAY; } base->items[i] = value; }",
        "static MGValue mg_length(MGValue a) { return mg_int(a.count ? (int64_t)a.count : (int64_t)a.n); }",
        "static MGValue mg_dot(MGValue a, MGValue b) { double sum = 0.0; int i, n = a.n < b.n ? a.n : b.n; for (i = 0; i < n; i++) sum += mg_component(a, i) * mg_component(b, i); return mg_float(sum); }",
        "static MGValue mg_normalize(MGValue a) { double sum = 0.0; double scale; MGValue v = a; int i; for (i = 0; i < a.n && i < 4; i++) sum += a.f[i] * a.f[i]; scale = sqrt(sum); if (scale == 0.0) return v; for (i = 0; i < a.n && i < 4; i++) v.f[i] /= scale; return v; }",
        "static MGValue mg_texture(MGValue sampler, MGValue coord) { (void)sampler; (void)coord; return mg_make_vec(4, 0, NULL); }",
        "static MGValue mg_unknown_call(int argc, const MGValue *args) { (void)argc; (void)args; return mg_zero(); }",
        "static void mg_discard(void) { mg_discarded = 1; }",
        "",
        0
    };
    int i;
    for (i = 0; runtime[i]; i++)
        cgen_runtime_text(runtime[i]);
}

typedef struct {
    char **values;
    int count;
    int cap;
} PendingArgs;

static void pending_free(PendingArgs *pending)
{
    int i;
    for (i = 0; i < pending->count; i++)
        free(pending->values[i]);
    free(pending->values);
    pending->values = 0;
    pending->count = 0;
    pending->cap = 0;
}

static void pending_add(PendingArgs *pending, const char *value)
{
    char **grown;
    if (pending->count == pending->cap) {
        pending->cap = pending->cap ? pending->cap * 2 : 8;
        grown = (char **)realloc(pending->values,
                                 (size_t)pending->cap * sizeof(*pending->values));
        if (!grown)
            return;
        pending->values = grown;
    }
    pending->values[pending->count++] = dup_cgen(value ? value : "");
}

static const char *pending_array(PendingArgs *pending)
{
    static char buffers[4][4096];
    static int next_buffer;
    char *out;
    int i;

    next_buffer = (next_buffer + 1) % 4;
    out = buffers[next_buffer];
    if (!pending->count) {
        strcpy(out, "NULL");
        return out;
    }
    strcpy(out, "(MGValue[]){");
    for (i = 0; i < pending->count; i++) {
        if (i)
            strcat(out, ", ");
        strcat(out, value_expr(pending->values[i]));
    }
    strcat(out, "}");
    return out;
}

static const char *function_source_name(const char *name)
{
    static char buffer[256];
    const char *paren = name ? strchr(name, '(') : 0;
    size_t n = paren ? (size_t)(paren - name) : strlen(name ? name : "");
    if (n >= sizeof(buffer))
        n = sizeof(buffer) - 1;
    memcpy(buffer, name ? name : "", n);
    buffer[n] = '\0';
    return buffer;
}

static int function_has_init_globals(void)
{
    int i;
    for (i = 0; i < n_optimized_quads; i++)
        if (!strcmp(optimized_quads[i].op, "func") &&
            !strcmp(optimized_quads[i].arg1, "__init_globals"))
            return 1;
    return 0;
}

static void emit_global_declarations(void)
{
    Symbol *s;
    int emitted = 0;
    for (s = symtab_first_symbol(); s; s = s->log_next) {
        if (s->depth != 0 || (s->kind != SYM_VAR && s->kind != SYM_BUILTIN))
            continue;
        if (!emitted) {
            cgen_line("/* Shader globals are MGValue objects so the generated program is self-contained. */");
            emitted = 1;
        }
        cgen_linef("static MGValue %s;", c_identifier(s->irname));
    }
    if (emitted)
        cgen_line("");
}

static int is_param_for_function(Symbol *s, const char *function_name)
{
    const char *source_name;
    if (!s || s->kind != SYM_PARAM || !s->scope_name)
        return 0;
    source_name = function_source_name(function_name);
    return !strcmp(s->scope_name, source_name);
}

static void emit_function_parameters(const char *function_name)
{
    Symbol *s;
    int index = 0;
    for (s = symtab_first_symbol(); s; s = s->log_next) {
        if (!is_param_for_function(s, function_name))
            continue;
        cgen_linef("    %s = argc > %d ? argv[%d] : mg_zero();",
                   c_identifier(s->irname), index, index);
        index++;
    }
}

static void emit_function_declarations(const char *function_name, int start, int end)
{
    char *names[512];
    int count = 0;
    int i;
    Symbol *s;

    for (s = symtab_first_symbol(); s; s = s->log_next)
        if (is_param_for_function(s, function_name))
            add_declaration(names, &count, 512, s->irname);
    for (i = start; i < end; i++)
        add_quad_declarations(&optimized_quads[i], names, &count, 512);
    for (i = 0; i < count; i++)
        cgen_linef("    MGValue %s = mg_zero();", c_identifier(names[i]));
    emit_function_parameters(function_name);
    free_declarations(names, count);
}

static void emit_call(PendingArgs *pending, Quad *q)
{
    const char *dest = q->result[0] ? c_identifier(q->result) : 0;
    const char *args = pending_array(pending);
    int argc = pending->count;
    const char *call_name = q->arg1;
    char expression[4096];

    if (!strcmp(call_name, "normalize") && argc >= 1)
        sprintf(expression, "mg_normalize(%s)", value_expr(pending->values[0]));
    else if (!strcmp(call_name, "dot") && argc >= 2)
        sprintf(expression, "mg_dot(%s, %s)", value_expr(pending->values[0]),
                value_expr(pending->values[1]));
    else if (!strcmp(call_name, "length") && argc >= 1)
        sprintf(expression, "mg_length(%s)", value_expr(pending->values[0]));
    else if ((!strcmp(call_name, "texture") || !strcmp(call_name, "texture2D") ||
              !strcmp(call_name, "textureCube") || !strcmp(call_name, "textureLod")) &&
             argc >= 2)
        sprintf(expression, "mg_texture(%s, %s)", value_expr(pending->values[0]),
                value_expr(pending->values[1]));
    else if (!strcmp(call_name, "max") && argc >= 2)
        sprintf(expression, "mg_maxv(%s, %s)", value_expr(pending->values[0]),
                value_expr(pending->values[1]));
    else if (!strcmp(call_name, "min") && argc >= 2)
        sprintf(expression, "mg_minv(%s, %s)", value_expr(pending->values[0]),
                value_expr(pending->values[1]));
    else if (!strcmp(call_name, "clamp") && argc >= 3)
        sprintf(expression, "mg_clampv(%s, %s, %s)", value_expr(pending->values[0]),
                value_expr(pending->values[1]), value_expr(pending->values[2]));
    else if (!strcmp(call_name, "mix") && argc >= 3)
        sprintf(expression, "mg_mixv(%s, %s, %s)", value_expr(pending->values[0]),
                value_expr(pending->values[1]), value_expr(pending->values[2]));
    else if (!strcmp(call_name, "abs") && argc >= 1)
        sprintf(expression, "mg_absv(%s)", value_expr(pending->values[0]));
    else if (!strcmp(call_name, "sqrt") && argc >= 1)
        sprintf(expression, "mg_sqrtv(%s)", value_expr(pending->values[0]));
    else if (func_first(function_source_name(call_name)))
        sprintf(expression, "%s(%d, %s)", c_function_name(call_name), argc, args);
    else
        sprintf(expression, "mg_unknown_call(%d, %s)", argc, args);

    if (dest)
        cgen_linef("    %s = %s;", dest, expression);
    else
        cgen_linef("    (void)%s;", expression);
    pending_free(pending);
}

static int constructor_width(const char *type)
{
    if (!strncmp(type, "vec", 3) || !strncmp(type, "bvec", 4) ||
        !strncmp(type, "ivec", 4) || !strncmp(type, "uvec", 4))
        return atoi(type + (type[0] == 'v' ? 3 : 4));
    if (!strncmp(type, "mat", 3)) {
        int n = atoi(type + 3);
        return n * n;
    }
    return 1;
}

static void emit_constructor(PendingArgs *pending, Quad *q)
{
    cgen_linef("    %s = mg_make_vec(%d, %d, %s);", c_identifier(q->result),
               constructor_width(q->arg1), pending->count, pending_array(pending));
    pending_free(pending);
}

static void emit_quad(PendingArgs *pending, Quad *q)
{
    const char *a1;
    const char *a2;
    const char *res;

    if (!strcmp(q->op, "param") || !strcmp(q->op, "refparam")) {
        pending_add(pending, q->arg1);
        return;
    }
    if (!strcmp(q->op, "construct")) {
        emit_constructor(pending, q);
        return;
    }
    if (!strcmp(q->op, "call")) {
        emit_call(pending, q);
        return;
    }
    if (pending->count)
        pending_free(pending);

    a1 = value_expr(q->arg1);
    a2 = value_expr(q->arg2);
    res = c_identifier(q->result);

    if (!strcmp(q->op, "func") || !strcmp(q->op, "endfunc")) {
        return;
    } else if (!strcmp(q->op, "label")) {
        cgen_linef("%s:", c_label_name(q->result));
    } else if (!strcmp(q->op, "goto")) {
        cgen_linef("    goto %s;", c_label_name(q->result));
    } else if (!strcmp(q->op, "if")) {
        cgen_linef("    if (mg_truth(%s)) goto %s;", a1, c_label_name(q->result));
    } else if (!strcmp(q->op, "ifFalse")) {
        cgen_linef("    if (!mg_truth(%s)) goto %s;", a1, c_label_name(q->result));
    } else if (!strcmp(q->op, "return")) {
        if (q->arg1[0])
            cgen_linef("    return %s;", a1);
        else
            cgen_line("    return mg_zero();");
    } else if (!strcmp(q->op, "discard")) {
        cgen_line("    mg_discard();");
        cgen_line("    return mg_zero();");
    } else if (!strcmp(q->op, "=")) {
        cgen_linef("    %s = %s;", res, a1);
    } else if (!strcmp(q->op, "+")) {
        cgen_linef("    %s = mg_add(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "-")) {
        cgen_linef("    %s = mg_sub(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "*")) {
        cgen_linef("    %s = mg_mul(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "/")) {
        cgen_linef("    %s = mg_div(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "%")) {
        cgen_linef("    %s = mg_mod(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "&")) {
        cgen_linef("    %s = mg_and(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "|")) {
        cgen_linef("    %s = mg_or(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "^") || !strcmp(q->op, "^^")) {
        cgen_linef("    %s = mg_xor(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "<<")) {
        cgen_linef("    %s = mg_shl(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, ">>")) {
        cgen_linef("    %s = mg_shr(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "&&")) {
        cgen_linef("    %s = mg_land(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "||")) {
        cgen_linef("    %s = mg_lor(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "<")) {
        cgen_linef("    %s = mg_lt(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, ">")) {
        cgen_linef("    %s = mg_gt(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "<=")) {
        cgen_linef("    %s = mg_le(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, ">=")) {
        cgen_linef("    %s = mg_ge(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "==")) {
        cgen_linef("    %s = mg_eq(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "!=")) {
        cgen_linef("    %s = mg_ne(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "minus")) {
        cgen_linef("    %s = mg_neg(%s);", res, a1);
    } else if (!strcmp(q->op, "!")) {
        cgen_linef("    %s = mg_not(%s);", res, a1);
    } else if (!strcmp(q->op, "~")) {
        cgen_linef("    %s = mg_bitnot(%s);", res, a1);
    } else if (!strcmp(q->op, "conv")) {
        cgen_linef("    %s = mg_conv(%s, \"%s\");", res, a1, q->arg2);
    } else if (!strcmp(q->op, "swizzle") || !strcmp(q->op, "member")) {
        cgen_linef("    %s = mg_%s(%s, \"%s\");", res,
                   !strcmp(q->op, "swizzle") ? "swizzle" : "member", a1, q->arg2);
    } else if (!strcmp(q->op, "swizzle=") || !strcmp(q->op, "member=")) {
        cgen_linef("    mg_%s_set(&%s, \"%s\", %s);",
                   !strcmp(q->op, "swizzle=") ? "swizzle" : "member", res, q->arg2, a1);
    } else if (!strcmp(q->op, "[]")) {
        cgen_linef("    %s = mg_loadidx(%s, %s);", res, a1, a2);
    } else if (!strcmp(q->op, "[]=")) {
        cgen_linef("    mg_storeidx(&%s, %s, %s);", res, a2, a1);
    } else if (!strcmp(q->op, "length")) {
        cgen_linef("    %s = mg_length(%s);", res, a1);
    } else {
        cgen_linef("    %s = mg_unknown_call(0, NULL); /* unsupported IR op: %s */",
                   res, q->op);
    }
}

static void emit_function(int start, int end, const char *function_name)
{
    PendingArgs pending = { 0, 0, 0 };
    int i;
    const char *cname = c_function_name(function_name);

    cgen_linef("static MGValue %s(int argc, const MGValue *argv)", cname);
    cgen_line("{");
    emit_function_declarations(function_name, start, end);
    for (i = start; i < end; i++)
        emit_quad(&pending, &optimized_quads[i]);
    pending_free(&pending);
    cgen_line("    return mg_zero();");
    cgen_line("}");
    cgen_line("");
}

void c_codegen_generate(void)
{
    int i, end;

    c_codegen_free();
    emit_runtime();
    emit_global_declarations();

    cgen_line("/* Function prototypes make calls independent of source order. */");
    for (i = 0; i < n_optimized_quads; i++) {
        if (!strcmp(optimized_quads[i].op, "func"))
            cgen_linef("static MGValue %s(int argc, const MGValue *argv);",
                       c_function_name(optimized_quads[i].arg1));
    }
    cgen_line("");

    for (i = 0; i < n_optimized_quads; i++) {
        if (strcmp(optimized_quads[i].op, "func") != 0)
            continue;
        end = i + 1;
        while (end < n_optimized_quads && strcmp(optimized_quads[end].op, "endfunc") != 0)
            end++;
        emit_function(i + 1, end, optimized_quads[i].arg1);
        i = end;
    }

    cgen_line("int main(void)");
    cgen_line("{");
    if (function_has_init_globals())
        cgen_line("    (void)shader___init_globals(0, NULL);");
    cgen_line("    (void)shader_main(0, NULL);");
    cgen_line("    return mg_discarded ? 0 : 0;");
    cgen_line("}");
}

void c_codegen_print(void)
{
    int i;
    for (i = 0; i < n_source_lines; i++)
        printf("%s\n", source_lines[i]);
}

int c_codegen_write_file(const char *path)
{
    FILE *out;
    int i;
    if (!path || !*path)
        return 0;
    out = fopen(path, "w");
    if (!out)
        return 0;
    for (i = 0; i < n_source_lines; i++)
        fprintf(out, "%s\n", source_lines[i]);
    if (fclose(out) != 0)
        return 0;
    return 1;
}
