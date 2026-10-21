/*
 * codegen.c -- Phase 6 target generation.
 *
 * This is intentionally an educational vector machine.  ADD, MUL and the
 * other arithmetic instructions operate element-wise according to the GLSL
 * operand type; they are not claims about a real GPU instruction encoding.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"
#include "optimizer.h"
#include "symtab.h"

static char **lines = 0;
static int n_lines = 0, cap_lines = 0;

static char **pending = 0;
static char **pending_source = 0;
static int *pending_ref = 0;
static int n_pending = 0, cap_pending = 0;

static char *sdup_codegen(const char *s)
{
    return strdup(s ? s : "");
}

static void clear_pending(void)
{
    int i;
    for (i = 0; i < n_pending; i++)
        free(pending[i]);
    for (i = 0; i < n_pending; i++)
        free(pending_source[i]);
    n_pending = 0;
}

void codegen_free(void)
{
    int i;
    for (i = 0; i < n_lines; i++)
        free(lines[i]);
    free(lines);
    lines = 0;
    n_lines = cap_lines = 0;
    clear_pending();
    free(pending);
    free(pending_source);
    free(pending_ref);
    pending = 0;
    pending_source = 0;
    pending_ref = 0;
    cap_pending = 0;
}

static void add_line(const char *text)
{
    if (n_lines == cap_lines) {
        cap_lines = cap_lines ? cap_lines * 2 : 128;
        lines = (char **)realloc(lines, (size_t)cap_lines * sizeof(*lines));
    }
    lines[n_lines++] = sdup_codegen(text);
}

static int is_temp(const char *s)
{
    int i;
    if (!s || s[0] != 't' || !s[1])
        return 0;
    for (i = 1; s[i]; i++)
        if (s[i] < '0' || s[i] > '9')
            return 0;
    return 1;
}

static const char *operand(const char *s)
{
    static char buffers[8][128];
    static int next;
    int k;
    if (!is_temp(s))
        return s ? s : "";
    k = atoi(s + 1);
    next = (next + 1) % 8;
    sprintf(buffers[next], "R%d", k - 1);
    return buffers[next];
}

static void add_pending(const char *value, int ref)
{
    if (n_pending == cap_pending) {
        cap_pending = cap_pending ? cap_pending * 2 : 16;
        pending = (char **)realloc(pending, (size_t)cap_pending * sizeof(*pending));
        pending_source = (char **)realloc(pending_source, (size_t)cap_pending * sizeof(*pending_source));
        pending_ref = (int *)realloc(pending_ref, (size_t)cap_pending * sizeof(*pending_ref));
    }
    pending[n_pending] = sdup_codegen(operand(value));
    pending_source[n_pending] = sdup_codegen(value);
    pending_ref[n_pending++] = ref;
}

static int vector_width_from_type(const char *name)
{
    Symbol *s = sym_lookup_ir(name);
    if (s && is_vector(s->type))
        return s->type.vec;
    return 0;
}

static int vector_width(const char *name)
{
    int i;
    int width = vector_width_from_type(name);
    if (width)
        return width;
    for (i = 0; i < n_optimized_quads; i++) {
        Quad *q = &optimized_quads[i];
        if (strcmp(q->result, name) != 0)
            continue;
        if (!strcmp(q->op, "swizzle"))
            return (int)strlen(q->arg2);
        if (!strcmp(q->op, "construct")) {
            if (!strcmp(q->arg1, "vec2")) return 2;
            if (!strcmp(q->arg1, "vec3")) return 3;
            if (!strcmp(q->arg1, "vec4")) return 4;
        }
    }
    return 0;
}

static void emit_pending_pushes(void)
{
    int i;
    char buf[256];
    for (i = 0; i < n_pending; i++) {
        sprintf(buf, "%s %s", pending_ref[i] ? "PUSHREF" : "PUSH", pending[i]);
        add_line(buf);
    }
}

static const char *binary_instruction(const char *op)
{
    if (!strcmp(op, "+")) return "ADD";
    if (!strcmp(op, "-")) return "SUB";
    if (!strcmp(op, "*")) return "MUL";
    if (!strcmp(op, "/")) return "DIV";
    if (!strcmp(op, "%")) return "MOD";
    if (!strcmp(op, "&")) return "AND";
    if (!strcmp(op, "|")) return "OR";
    if (!strcmp(op, "^") || !strcmp(op, "^^")) return "XOR";
    if (!strcmp(op, "<<")) return "SHL";
    if (!strcmp(op, ">>")) return "SHR";
    if (!strcmp(op, "&&")) return "AND";
    if (!strcmp(op, "||")) return "OR";
    return 0;
}

static const char *comparison_instruction(const char *op)
{
    if (!strcmp(op, "<")) return "CMP_LT";
    if (!strcmp(op, ">")) return "CMP_GT";
    if (!strcmp(op, "<=")) return "CMP_LE";
    if (!strcmp(op, ">=")) return "CMP_GE";
    if (!strcmp(op, "==")) return "CMP_EQ";
    if (!strcmp(op, "!=")) return "CMP_NE";
    return 0;
}

static void emit_constructor(Quad *q)
{
    const char *type = q->arg1;
    const char *inst = 0;
    char buf[1024];
    int i;
    if (!strcmp(type, "vec2")) inst = "VEC2";
    else if (!strcmp(type, "vec3")) inst = "VEC3";
    else if (!strcmp(type, "vec4")) inst = "VEC4";
    else if (!strcmp(type, "mat2")) inst = "MAT2";
    else if (!strcmp(type, "mat3")) inst = "MAT3";
    else if (!strcmp(type, "mat4")) inst = "MAT4";
    if (!inst) inst = "CONSTRUCT";
    sprintf(buf, "%s %s", inst, operand(q->result));
    for (i = 0; i < n_pending; i++) {
        strcat(buf, i == 0 ? ", " : ", ");
        strcat(buf, pending[i]);
    }
    add_line(buf);
    clear_pending();
}

static void emit_call(Quad *q)
{
    char buf[1024];
    int i;
    const char *dest = q->result[0] ? operand(q->result) : "";

    if (!strcmp(q->arg1, "normalize") && n_pending >= 1) {
        sprintf(buf, "NORM %s, %s", dest, pending[0]);
        add_line(buf);
    } else if ((!strcmp(q->arg1, "texture") || !strcmp(q->arg1, "texture2D") ||
                !strcmp(q->arg1, "textureCube") || !strcmp(q->arg1, "textureLod")) && n_pending >= 2) {
        sprintf(buf, "TEX %s", dest);
        for (i = 0; i < n_pending; i++) {
            strcat(buf, ", ");
            strcat(buf, pending[i]);
        }
        add_line(buf);
    } else if (!strcmp(q->arg1, "length") && n_pending >= 1) {
        sprintf(buf, "LEN %s, %s", dest, pending[0]);
        add_line(buf);
    } else if (!strcmp(q->arg1, "dot") && n_pending >= 2 &&
               vector_width(pending_source[0]) == vector_width(pending_source[1]) &&
               (vector_width(pending_source[0]) == 3 || vector_width(pending_source[0]) == 4)) {
        sprintf(buf, "DP%d %s, %s, %s", vector_width(pending_source[0]), dest,
                pending[0], pending[1]);
        add_line(buf);
    } else {
        emit_pending_pushes();
        sprintf(buf, "CALL %s", q->arg1);
        add_line(buf);
        if (dest[0]) {
            sprintf(buf, "MOV %s, RETVAL", dest);
            add_line(buf);
        }
    }
    clear_pending();
}

static void emit_quad(Quad *q)
{
    char buf[1024];
    const char *a1 = operand(q->arg1);
    const char *a2 = operand(q->arg2);
    const char *res = operand(q->result);
    const char *inst;

    if (!strcmp(q->op, "param")) { add_pending(q->arg1, 0); return; }
    if (!strcmp(q->op, "refparam")) { add_pending(q->arg1, 1); return; }
    if (!strcmp(q->op, "construct")) { emit_constructor(q); return; }
    if (!strcmp(q->op, "call")) { emit_call(q); return; }

    if (n_pending) {
        emit_pending_pushes();
        clear_pending();
    }

    if (!strcmp(q->op, "func")) sprintf(buf, "FUNC %s", q->arg1);
    else if (!strcmp(q->op, "endfunc")) {
        if (n_lines == 0 || strncmp(lines[n_lines - 1], "RET", 3) != 0)
            add_line("RET");
        sprintf(buf, "END %s", q->arg1);
    }
    else if (!strcmp(q->op, "label")) sprintf(buf, "LABEL %s", q->result);
    else if (!strcmp(q->op, "goto")) sprintf(buf, "JMP %s", q->result);
    else if (!strcmp(q->op, "if")) sprintf(buf, "JTRUE %s, %s", a1, q->result);
    else if (!strcmp(q->op, "ifFalse")) sprintf(buf, "JFALSE %s, %s", a1, q->result);
    else if (!strcmp(q->op, "return")) {
        if (q->arg1[0]) sprintf(buf, "RET %s", a1);
        else strcpy(buf, "RET");
    } else if (!strcmp(q->op, "discard")) strcpy(buf, "DISCARD");
    else if (!strcmp(q->op, "=")) sprintf(buf, "MOV %s, %s", res, a1);
    else if (!strcmp(q->op, "minus")) sprintf(buf, "NEG %s, %s", res, a1);
    else if (!strcmp(q->op, "!") || !strcmp(q->op, "~")) sprintf(buf, "NOT %s, %s", res, a1);
    else if (!strcmp(q->op, "conv")) sprintf(buf, "CONV %s, %s, %s", res, a1, q->arg2);
    else if (!strcmp(q->op, "swizzle") || !strcmp(q->op, "member"))
        sprintf(buf, "MOV %s, %s.%s", res, a1, q->arg2);
    else if (!strcmp(q->op, "swizzle=") || !strcmp(q->op, "member="))
        sprintf(buf, "MOV %s.%s, %s", q->result, q->arg2, a1);
    else if (!strcmp(q->op, "[]")) sprintf(buf, "LOADIDX %s, %s, %s", res, a1, a2);
    else if (!strcmp(q->op, "[]=")) sprintf(buf, "STOREIDX %s, %s, %s", q->result, a2, a1);
    else if (!strcmp(q->op, "length")) sprintf(buf, "ARRAYLEN %s, %s", res, a1);
    else if ((inst = comparison_instruction(q->op)) != 0)
        sprintf(buf, "%s %s, %s, %s", inst, res, a1, a2);
    else if ((inst = binary_instruction(q->op)) != 0)
        sprintf(buf, "%s %s, %s, %s", inst, res, a1, a2);
    else
        sprintf(buf, "%s %s, %s, %s", q->op, res, a1, a2);
    add_line(buf);
}

void codegen_generate(void)
{
    int i;
    codegen_free();
    for (i = 0; i < n_optimized_quads; i++)
        emit_quad(&optimized_quads[i]);
    if (n_pending) {
        emit_pending_pushes();
        clear_pending();
    }
}

void codegen_print(void)
{
    int i;
    for (i = 0; i < n_lines; i++)
        printf("%4d  %s\n", i, lines[i]);
}
