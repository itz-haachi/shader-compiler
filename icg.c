/*
 * icg.c -- Phase 4: intermediate code generation (three-address code).
 *
 * Walks the typed AST produced by the semantic analyzer and emits
 * quadruples. The instruction set:
 *
 *   result = arg1 op arg2       op: + - * / % << >> & | ^ ^^ < > <= >= == !=
 *   result = minus arg1         also: ! ~
 *   result = arg1               op "="  (copy)
 *   result = (type) arg1        op "conv", arg2 = target type
 *   result = arg1.arg2          op "swizzle" (components normalised to xyzw)
 *   result = arg1.arg2          op "member"  (struct member)
 *   result.arg2 = arg1          op "swizzle=" / "member="
 *   result = arg1[arg2]         op "[]"
 *   result[arg2] = arg1         op "[]="
 *   param arg1 / refparam arg1  argument (refparam: out/inout, by reference)
 *   result = call arg1, arg2    arg2 = number of arguments
 *   result = construct arg1, arg2   vector/matrix/struct constructor
 *   result = length arg1        runtime length of an unsized array
 *   label / goto / if / ifFalse  control flow
 *   return arg1 / discard
 *   func arg1 / endfunc arg1    function boundaries
 *
 * && and || are translated with short-circuit jumps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "symtab.h"
#include "icg.h"

Quad *quads = 0;
int n_quads = 0;
static int cap_quads = 0;

static Type *temp_types = 0;
static int *temp_dropped = 0;
static int n_temps = 0, cap_temps = 0;
static int n_labels = 0;

#define MAX_NEST 64
static char *break_label[MAX_NEST];
static int n_break = 0;
static char *continue_label[MAX_NEST];
static int n_continue = 0;

static char *gen_expr(Node *n);
static void gen_stmt(Node *n);

/* ---------------- emission helpers ---------------- */

static char *dup(const char *s)
{
    return strdup(s ? s : "");
}

static void emit(char *op, char *a1, char *a2, char *res)
{
    if (n_quads == cap_quads) {
        cap_quads = cap_quads ? cap_quads * 2 : 256;
        quads = (Quad *)realloc(quads, cap_quads * sizeof(Quad));
    }
    quads[n_quads].op = dup(op);
    quads[n_quads].arg1 = dup(a1);
    quads[n_quads].arg2 = dup(a2);
    quads[n_quads].result = dup(res);
    n_quads++;
}

static char *new_temp(Type t)
{
    char buf[32];
    if (n_temps == cap_temps) {
        cap_temps = cap_temps ? cap_temps * 2 : 128;
        temp_types = (Type *)realloc(temp_types, cap_temps * sizeof(Type));
        temp_dropped = (int *)realloc(temp_dropped, cap_temps * sizeof(int));
    }
    temp_types[n_temps] = t;
    temp_dropped[n_temps] = 0;
    n_temps++;
    sprintf(buf, "t%d", n_temps);
    return dup(buf);
}

static char *new_label(void)
{
    char buf[32];
    sprintf(buf, "L%d", ++n_labels);
    return dup(buf);
}

static char *num(int k)
{
    char buf[32];
    sprintf(buf, "%d", k);
    return dup(buf);
}

static int temp_index(const char *s)
{
    int k;
    if (s[0] != 't' || sscanf(s + 1, "%d", &k) != 1)
        return -1;
    return k >= 1 && k <= n_temps ? k - 1 : -1;
}

/*
 * target = val. If val is the temporary computed by the previous
 * quadruple, write the result straight into target instead of adding a
 * copy (t1 = a + b; x = t1  ==>  x = a + b).
 */
static char *assign_to(char *target, char *val)
{
    int k = temp_index(val);
    Quad *last = n_quads ? &quads[n_quads - 1] : 0;
    if (k >= 0 && last && strcmp(last->result, val) == 0
        && strcmp(last->op, "label") != 0 && strcmp(last->op, "if") != 0
        && strcmp(last->op, "ifFalse") != 0 && strcmp(last->op, "goto") != 0) {
        last->result = dup(target);
        temp_dropped[k] = 1;
    } else {
        emit("=", val, "", target);
    }
    return target;
}

static char *ir_name(Node *id)
{
    return id->sym && id->sym->irname ? id->sym->irname : id->sval;
}

static char *one_for(Type t)
{
    if (t.base == TY_FLOAT || t.base == TY_DOUBLE) return "1.0";
    if (t.base == TY_UINT) return "1u";
    return "1";
}

/* swizzles are written with x y z w in the intermediate code */
static char *norm_swizzle(char *s)
{
    char *r = dup(s);
    int i;
    for (i = 0; r[i]; i++) {
        switch (r[i]) {
        case 'r': case 's': r[i] = 'x'; break;
        case 'g': case 't': r[i] = 'y'; break;
        case 'b': case 'p': r[i] = 'z'; break;
        case 'a': case 'q': r[i] = 'w'; break;
        }
    }
    return r;
}

/* ---------------- stores to lvalues ---------------- */

/* store val into the lvalue expression lv; returns an operand holding the value */
static char *gen_store(Node *lv, char *val)
{
    char *base, *idx;

    switch (lv->kind) {
    case N_ID:
        return assign_to(ir_name(lv), val);
    case N_INDEX:
        if (lv->a->kind == N_ID) {
            idx = gen_expr(lv->b);
            emit("[]=", val, idx, ir_name(lv->a));
        } else {                 /* read-modify-write of the enclosing value */
            base = gen_expr(lv->a);
            idx = gen_expr(lv->b);
            emit("[]=", val, idx, base);
            gen_store(lv->a, base);
        }
        return val;
    case N_FIELD:
        if (lv->a->kind == N_ID) {
            emit(lv->is_swizzle ? "swizzle=" : "member=", val,
                 lv->is_swizzle ? norm_swizzle(lv->sval) : lv->sval, ir_name(lv->a));
        } else {
            base = gen_expr(lv->a);
            emit(lv->is_swizzle ? "swizzle=" : "member=", val,
                 lv->is_swizzle ? norm_swizzle(lv->sval) : lv->sval, base);
            gen_store(lv->a, base);
        }
        return val;
    case N_CONV:
        return gen_store(lv->a, val);
    default:
        return val;
    }
}

/* ---------------- expressions ---------------- */

static char *gen_short_circuit(Node *n)
{
    char *res = new_temp(n->type);
    char *end = new_label();
    char *a = gen_expr(n->a), *b;

    assign_to(res, a);
    emit(n->op == OP_LAND ? "ifFalse" : "if", res, "", end);
    b = gen_expr(n->b);
    assign_to(res, b);
    emit("label", "", "", end);
    return res;
}

/* evaluate the arguments, then emit the param instructions */
static int gen_args(Node *args, Symbol *f, Node **list, char **vals, int *is_ref)
{
    Node *a;
    int n = 0, out;
    for (a = args; a && n < 32; a = a->next, n++) {
        out = f && n < f->nparams && (f->pquals[n] & (Q_OUT | Q_INOUT));
        list[n] = a;
        is_ref[n] = out;
        if (out && a->kind == N_ID) {
            vals[n] = ir_name(a);
        } else if (out) {
            /* complex lvalue: pass a temporary and copy it back later */
            vals[n] = new_temp(a->type);
            if (f->pquals[n] & Q_INOUT)
                emit("=", gen_expr(a), "", vals[n]);
        } else {
            vals[n] = gen_expr(a);
        }
    }
    for (out = 0; out < n; out++)
        emit(is_ref[out] ? "refparam" : "param", vals[out], "", "");
    return n;
}

static char *gen_expr(Node *n)
{
    char *a, *b, *t, *lelse, *lend, *old;
    Node *list[32];
    char *vals[32];
    int is_ref[32], k, i;

    switch (n->kind) {
    case N_ID:
        return ir_name(n);
    case N_INTLIT: case N_UINTLIT: case N_FLOATLIT: case N_BOOLLIT:
        return n->sval;

    case N_CONV:
        a = gen_expr(n->a);
        t = new_temp(n->type);
        emit("conv", a, type_str(n->type), t);
        return t;

    case N_BINARY:
        if (n->op == OP_LAND || n->op == OP_LOR)
            return gen_short_circuit(n);
        a = gen_expr(n->a);
        b = gen_expr(n->b);
        t = new_temp(n->type);
        emit(op_str(n->op), a, b, t);
        return t;

    case N_UNARY:
        switch (n->op) {
        case OP_POS:
            return gen_expr(n->a);
        case OP_NEG: case OP_NOT: case OP_BNOT:
            a = gen_expr(n->a);
            t = new_temp(n->type);
            emit(n->op == OP_NEG ? "minus" : op_str(n->op), a, "", t);
            return t;
        case OP_INC: case OP_DEC:            /* ++x : x = x + 1, value is new x */
            a = gen_expr(n->a);
            t = new_temp(n->type);
            emit(n->op == OP_INC ? "+" : "-", a, one_for(n->type), t);
            return gen_store(n->a, t);
        }
        return "";

    case N_POSTFIX:                          /* x++ : old = x; x = old + 1; value is old */
        a = gen_expr(n->a);
        old = new_temp(n->type);
        emit("=", a, "", old);
        t = new_temp(n->type);
        emit(n->op == OP_INC ? "+" : "-", old, one_for(n->type), t);
        gen_store(n->a, t);
        return old;

    case N_ASSIGN:
        if (n->op == OP_ASSIGN) {
            b = gen_expr(n->b);
            return gen_store(n->a, b);
        }
        a = gen_expr(n->a);
        b = gen_expr(n->b);
        t = new_temp(n->type);
        emit(op_str(n->op), a, b, t);
        return gen_store(n->a, t);

    case N_TERNARY:
        t = new_temp(n->type);
        lelse = new_label();
        lend = new_label();
        a = gen_expr(n->a);
        emit("ifFalse", a, "", lelse);
        assign_to(t, gen_expr(n->b));
        emit("goto", "", "", lend);
        emit("label", "", "", lelse);
        assign_to(t, gen_expr(n->c));
        emit("label", "", "", lend);
        return t;

    case N_INDEX:
        a = gen_expr(n->a);
        b = gen_expr(n->b);
        t = new_temp(n->type);
        emit("[]", a, b, t);
        return t;

    case N_FIELD:
        a = gen_expr(n->a);
        t = new_temp(n->type);
        if (n->is_swizzle)
            emit("swizzle", a, norm_swizzle(n->sval), t);
        else
            emit("member", a, n->sval, t);
        return t;

    case N_CALL:
        k = gen_args(n->a, n->sym, list, vals, is_ref);
        t = is_void(n->type) ? "" : new_temp(n->type);
        emit("call", n->sym ? n->sym->irname : n->sval, num(k), t);
        for (i = 0; i < k; i++)              /* copy back out-arguments */
            if (is_ref[i] && list[i]->kind != N_ID)
                gen_store(list[i], vals[i]);
        return t;

    case N_METHOD:                           /* arr.length() */
        if (n->a->type.arr > 0)
            return num(n->a->type.arr);
        if (is_vector(n->a->type))
            return num(n->a->type.vec);
        if (is_matrix(n->a->type))
            return num(n->a->type.cols);
        a = gen_expr(n->a);
        t = new_temp(n->type);
        emit("length", a, "", t);
        return t;

    case N_CTOR:
        k = gen_args(n->a, 0, list, vals, is_ref);
        t = new_temp(n->type);
        emit("construct", type_str(n->type), num(k), t);
        return t;

    case N_COMMA:
        gen_expr(n->a);
        return gen_expr(n->b);

    default:
        return "";
    }
}

/* ---------------- declarations ---------------- */

static void gen_init(char *target, Type t, Node *init)
{
    Node *e;
    int i;
    char *v;
    StructDef *sd;
    Type et;

    if (init->kind != N_INITLIST) {
        assign_to(target, gen_expr(init));
        return;
    }

    sd = t.base == TY_STRUCT && !is_array(t) ? struct_lookup(t.name) : 0;
    for (e = init->a, i = 0; e; e = e->next, i++) {
        if (sd)
            et = sd->mtypes[i];
        else if (is_array(t))
            et = elem_type(t);
        else if (is_matrix(t))
            et = mk_type(t.base, t.vec, 0);
        else
            et = mk_type(t.base, 1, 0);

        if (e->kind == N_INITLIST) {
            v = new_temp(et);
            gen_init(v, et, e);
        } else {
            v = gen_expr(e);
        }
        if (sd)
            emit("member=", v, sd->mnames[i], target);
        else
            emit("[]=", v, num(i), target);
    }
}

static void gen_decls(Node *list)
{
    for (; list; list = list->next)
        if (list->kind == N_VARDECL && list->a && list->sym)
            gen_init(list->sym->irname, list->type, list->a);
}

/* ---------------- statements ---------------- */

static void gen_cond_jump_false(Node *cond, char *label)
{
    char *c = gen_expr(cond);
    emit("ifFalse", c, "", label);
}

static void gen_switch(Node *n)
{
    char *v = gen_expr(n->a);
    char *lend = new_label();
    char *ldefault = 0, *t, buf[16];
    Node *s;

    /* give every case label of this switch a code label and test it */
    for (s = n->b->a; s; s = s->next) {
        if (s->kind != N_CASE && s->kind != N_DEFAULT)
            continue;
        s->label = ++n_labels;
        sprintf(buf, "L%d", s->label);
        if (s->kind == N_CASE) {
            t = new_temp(mk_type(TY_BOOL, 1, 0));
            emit("==", v, gen_expr(s->a), t);
            emit("if", t, "", buf);
        } else {
            ldefault = dup(buf);
        }
    }
    emit("goto", "", "", ldefault ? ldefault : lend);

    break_label[n_break++] = lend;
    for (s = n->b->a; s; s = s->next)
        gen_stmt(s);
    n_break--;
    emit("label", "", "", lend);
}

static void gen_stmt(Node *n)
{
    char *l1, *l2, *l3, *c, buf[16];

    if (n == 0)
        return;
    switch (n->kind) {
    case N_COMPOUND:
        for (n = n->a; n; n = n->next)
            gen_stmt(n);
        break;
    case N_DECLSTMT:
        gen_decls(n->a);
        break;
    case N_EXPRSTMT:
        if (n->a)
            gen_expr(n->a);
        break;
    case N_IF:
        l1 = new_label();
        gen_cond_jump_false(n->a, l1);
        gen_stmt(n->b);
        if (n->c) {
            l2 = new_label();
            emit("goto", "", "", l2);
            emit("label", "", "", l1);
            gen_stmt(n->c);
            emit("label", "", "", l2);
        } else {
            emit("label", "", "", l1);
        }
        break;
    case N_WHILE:
        l1 = new_label();
        l2 = new_label();
        emit("label", "", "", l1);
        gen_cond_jump_false(n->a, l2);
        break_label[n_break++] = l2;
        continue_label[n_continue++] = l1;
        gen_stmt(n->b);
        n_break--; n_continue--;
        emit("goto", "", "", l1);
        emit("label", "", "", l2);
        break;
    case N_DOWHILE:
        l1 = new_label();          /* body   */
        l2 = new_label();          /* condition (continue target) */
        l3 = new_label();          /* exit   */
        emit("label", "", "", l1);
        break_label[n_break++] = l3;
        continue_label[n_continue++] = l2;
        gen_stmt(n->a);
        n_break--; n_continue--;
        emit("label", "", "", l2);
        c = gen_expr(n->b);
        emit("if", c, "", l1);
        emit("label", "", "", l3);
        break;
    case N_FOR:
        l1 = new_label();          /* test */
        l2 = new_label();          /* step (continue target) */
        l3 = new_label();          /* exit */
        gen_stmt(n->a);
        emit("label", "", "", l1);
        if (n->b)
            gen_cond_jump_false(n->b, l3);
        break_label[n_break++] = l3;
        continue_label[n_continue++] = l2;
        gen_stmt(n->d);
        n_break--; n_continue--;
        emit("label", "", "", l2);
        if (n->c)
            gen_expr(n->c);
        emit("goto", "", "", l1);
        emit("label", "", "", l3);
        break;
    case N_SWITCH:
        gen_switch(n);
        break;
    case N_CASE: case N_DEFAULT:
        sprintf(buf, "L%d", n->label);
        emit("label", "", "", buf);
        break;
    case N_BREAK:
        if (n_break)
            emit("goto", "", "", break_label[n_break - 1]);
        break;
    case N_CONTINUE:
        if (n_continue)
            emit("goto", "", "", continue_label[n_continue - 1]);
        break;
    case N_RETURN:
        emit("return", n->a ? gen_expr(n->a) : "", "", "");
        break;
    case N_DISCARD:
        emit("discard", "", "", "");
        break;
    default:
        break;
    }
}

/* ---------------- program ---------------- */

/* temporaries removed by assign_to() leave gaps: renumber t1, t2, ... */
static char *renamed(char *s, int *map)
{
    int k = temp_index(s);
    char buf[32];
    if (k < 0 || map[k] < 0)
        return s;
    sprintf(buf, "t%d", map[k] + 1);
    return dup(buf);
}

static void renumber_temps(void)
{
    int *map = (int *)malloc((n_temps + 1) * sizeof(int));
    int i, k = 0;

    for (i = 0; i < n_temps; i++) {
        if (temp_dropped[i]) {
            map[i] = -1;
        } else {
            map[i] = k;
            temp_types[k] = temp_types[i];
            temp_dropped[k] = 0;
            k++;
        }
    }
    for (i = 0; i < n_quads; i++) {
        if (strcmp(quads[i].op, "call") != 0 && strcmp(quads[i].op, "func") != 0
            && strcmp(quads[i].op, "endfunc") != 0 && strcmp(quads[i].op, "construct") != 0)
            quads[i].arg1 = renamed(quads[i].arg1, map);
        if (strcmp(quads[i].op, "member") && strcmp(quads[i].op, "member=")
            && strcmp(quads[i].op, "swizzle") && strcmp(quads[i].op, "swizzle=")
            && strcmp(quads[i].op, "conv") && strcmp(quads[i].op, "call")
            && strcmp(quads[i].op, "construct"))
            quads[i].arg2 = renamed(quads[i].arg2, map);
        quads[i].result = renamed(quads[i].result, map);
    }
    n_temps = k;
    free(map);
}

void icg_generate(Node *program)
{
    Node *e;
    int has_globals = 0;

    /* initializers of global variables run first, in a pseudo-function */
    for (e = program->a; e; e = e->next)
        if (e->kind == N_VARDECL && e->a && e->sym)
            has_globals = 1;
    if (has_globals) {
        emit("func", "__init_globals", "", "");
        for (e = program->a; e; e = e->next)
            if (e->kind == N_VARDECL && e->a && e->sym)
                gen_init(e->sym->irname, e->type, e->a);
        emit("endfunc", "__init_globals", "", "");
    }

    for (e = program->a; e; e = e->next) {
        if (e->kind != N_FUNC || e->b == 0)
            continue;
        emit("func", e->sym ? e->sym->irname : e->sval, "", "");
        gen_stmt(e->b);
        emit("endfunc", e->sym ? e->sym->irname : e->sval, "", "");
    }

    renumber_temps();
}

/* ---------------- printing ---------------- */

static void tac_text(Quad *q, char *out)
{
    char *op = q->op;

    if (!strcmp(op, "label"))         sprintf(out, "%s:", q->result);
    else if (!strcmp(op, "func"))     sprintf(out, "func %s:", q->arg1);
    else if (!strcmp(op, "endfunc"))  sprintf(out, "endfunc %s", q->arg1);
    else if (!strcmp(op, "goto"))     sprintf(out, "    goto %s", q->result);
    else if (!strcmp(op, "if"))       sprintf(out, "    if %s goto %s", q->arg1, q->result);
    else if (!strcmp(op, "ifFalse"))  sprintf(out, "    ifFalse %s goto %s", q->arg1, q->result);
    else if (!strcmp(op, "="))        sprintf(out, "    %s = %s", q->result, q->arg1);
    else if (!strcmp(op, "conv"))     sprintf(out, "    %s = (%s) %s", q->result, q->arg2, q->arg1);
    else if (!strcmp(op, "minus"))    sprintf(out, "    %s = -%s", q->result, q->arg1);
    else if (!strcmp(op, "!") || !strcmp(op, "~"))
                                      sprintf(out, "    %s = %s%s", q->result, op, q->arg1);
    else if (!strcmp(op, "swizzle") || !strcmp(op, "member"))
                                      sprintf(out, "    %s = %s.%s", q->result, q->arg1, q->arg2);
    else if (!strcmp(op, "swizzle=") || !strcmp(op, "member="))
                                      sprintf(out, "    %s.%s = %s", q->result, q->arg2, q->arg1);
    else if (!strcmp(op, "[]"))       sprintf(out, "    %s = %s[%s]", q->result, q->arg1, q->arg2);
    else if (!strcmp(op, "[]="))      sprintf(out, "    %s[%s] = %s", q->result, q->arg2, q->arg1);
    else if (!strcmp(op, "param"))    sprintf(out, "    param %s", q->arg1);
    else if (!strcmp(op, "refparam")) sprintf(out, "    refparam %s", q->arg1);
    else if (!strcmp(op, "call")) {
        if (q->result[0])             sprintf(out, "    %s = call %s, %s", q->result, q->arg1, q->arg2);
        else                          sprintf(out, "    call %s, %s", q->arg1, q->arg2);
    }
    else if (!strcmp(op, "construct"))sprintf(out, "    %s = construct %s, %s", q->result, q->arg1, q->arg2);
    else if (!strcmp(op, "length"))   sprintf(out, "    %s = length %s", q->result, q->arg1);
    else if (!strcmp(op, "return"))   sprintf(out, "    return%s%s", q->arg1[0] ? " " : "", q->arg1);
    else if (!strcmp(op, "discard"))  sprintf(out, "    discard");
    else                              sprintf(out, "    %s = %s %s %s", q->result, q->arg1, op, q->arg2);
}

void icg_print_tac(void)
{
    int i;
    char line[512];
    for (i = 0; i < n_quads; i++) {
        tac_text(&quads[i], line);
        printf("%4d  %s\n", i, line);
    }
}

void icg_print_quads(void)
{
    int i;
    printf("%-5s | %-10s | %-16s | %-12s | %s\n", "#", "op", "arg1", "arg2", "result");
    printf("------+------------+------------------+--------------+-----------------\n");
    for (i = 0; i < n_quads; i++)
        printf("%-5d | %-10s | %-16s | %-12s | %s\n", i, quads[i].op,
               quads[i].arg1, quads[i].arg2, quads[i].result);
}

void icg_print_temps(void)
{
    int i, shown = 0;
    for (i = 0; i < n_temps; i++) {
        if (temp_dropped[i])
            continue;
        printf("  t%-4d : %-8s%s", i + 1, type_str(temp_types[i]),
               ++shown % 6 == 0 ? "\n" : "   ");
    }
    if (shown % 6)
        printf("\n");
    printf("(%d quadruples, %d temporaries, %d labels)\n", n_quads, shown, n_labels);
}
