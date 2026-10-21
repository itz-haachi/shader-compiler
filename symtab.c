/*
 * symtab.c -- scoped symbol table, function table and struct table.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtab.h"
#include "ast.h"

#define MAX_DEPTH 64

static Symbol *scopes[MAX_DEPTH];   /* scopes[d] = list of symbols at depth d */
static int depth = 0;
static char *cur_scope_name = "global";

static Symbol *sym_log = 0, *sym_log_tail = 0;
static Symbol *functions = 0;       /* all functions (also in the log) */
static StructDef *structs = 0, *structs_tail = 0;

/* names already used for IR variables inside the current function */
static char *ir_names[1024];
static int n_ir_names = 0;

static void log_symbol(Symbol *s)
{
    if (sym_log_tail)
        sym_log_tail->log_next = s;
    else
        sym_log = s;
    sym_log_tail = s;
}

void symtab_init(void)
{
    depth = 0;
    scopes[0] = 0;
}

void scope_push(void)
{
    if (depth + 1 < MAX_DEPTH)
        scopes[++depth] = 0;
}

void scope_pop(void)
{
    if (depth > 0)
        depth--;
}

int scope_depth(void)
{
    return depth;
}

void set_scope_name(char *name)
{
    cur_scope_name = name;
    n_ir_names = 0;     /* a new function starts a fresh set of IR names */
}

Symbol *sym_lookup_current(const char *name)
{
    Symbol *s;
    for (s = scopes[depth]; s; s = s->scope_next)
        if (strcmp(s->name, name) == 0)
            return s;
    return 0;
}

Symbol *sym_lookup(const char *name)
{
    int d;
    Symbol *s;
    for (d = depth; d >= 0; d--)
        for (s = scopes[d]; s; s = s->scope_next)
            if (strcmp(s->name, name) == 0)
                return s;
    return 0;
}

Symbol *sym_lookup_ir(const char *irname)
{
    return sym_lookup_ir_scope(irname, 0);
}

Symbol *sym_lookup_ir_scope(const char *irname, const char *scope_name)
{
    Symbol *s;
    Symbol *global = 0;
    if (!irname)
        return 0;
    for (s = sym_log; s; s = s->log_next)
        if (s->irname && strcmp(s->irname, irname) == 0) {
            if (scope_name && s->scope_name &&
                strcmp(s->scope_name, scope_name) == 0)
                return s;
            if (!global && s->scope_name &&
                strcmp(s->scope_name, "global") == 0)
                global = s;
        }
    return global;
}

Symbol *symtab_first_symbol(void)
{
    return sym_log;
}

/* pick a unique IR name: x, then x_1, x_2 ... for shadowing declarations */
static char *make_irname(char *name)
{
    int i, k = 0, clash;
    char buf[128];

    /* t<n> and L<n> are reserved for temporaries and labels in the IR */
    if ((name[0] == 't' || name[0] == 'L') && name[1] && strspn(name + 1, "0123456789") == strlen(name + 1)) {
        sprintf(buf, "%s_", name);
        name = strdup(buf);
    }
    if (depth == 0)
        return name;
    strcpy(buf, name);
    do {
        clash = 0;
        for (i = 0; i < n_ir_names; i++)
            if (strcmp(ir_names[i], buf) == 0)
                clash = 1;
        if (!clash && sym_lookup(buf) && sym_lookup(buf)->depth == 0)
            clash = 1;          /* would hide a global in the IR */
        if (clash)
            sprintf(buf, "%s_%d", name, ++k);
    } while (clash);

    if (n_ir_names < 1024)
        ir_names[n_ir_names++] = strdup(buf);
    return strdup(buf);
}

Symbol *sym_insert(char *name, SymKind kind, Type type, int quals, int line)
{
    Symbol *s = (Symbol *)calloc(1, sizeof(Symbol));
    s->name = name;
    s->kind = kind;
    s->type = type;
    s->quals = quals;
    s->line = line;
    s->depth = depth;
    s->scope_name = depth == 0 ? "global" : cur_scope_name;
    s->irname = make_irname(name);
    s->scope_next = scopes[depth];
    scopes[depth] = s;
    log_symbol(s);
    return s;
}

Symbol *func_insert(char *name, Type ret, int nparams, Type *ptypes, int *pquals, int line)
{
    int i;
    Symbol *s = (Symbol *)calloc(1, sizeof(Symbol));
    Symbol *p;

    s->name = name;
    s->kind = SYM_FUNC;
    s->type = ret;
    s->line = line;
    s->depth = 0;
    s->scope_name = "global";
    s->irname = name;
    s->nparams = nparams;
    for (i = 0; i < nparams && i < MAX_PARAMS; i++) {
        s->ptypes[i] = ptypes[i];
        s->pquals[i] = pquals[i];
    }

    /* functions are kept in declaration order */
    if (functions == 0) {
        functions = s;
    } else {
        for (p = functions; p->scope_next; p = p->scope_next)
            ;
        p->scope_next = s;
    }
    log_symbol(s);
    return s;
}

Symbol *func_first(const char *name)
{
    Symbol *f;
    for (f = functions; f; f = f->scope_next)
        if (strcmp(f->name, name) == 0)
            return f;
    return 0;
}

Symbol *func_next(Symbol *f)
{
    const char *name = f->name;
    for (f = f->scope_next; f; f = f->scope_next)
        if (strcmp(f->name, name) == 0)
            return f;
    return 0;
}

Symbol *func_find_exact(const char *name, int nparams, Type *ptypes)
{
    Symbol *f;
    int i, same;
    for (f = func_first(name); f; f = func_next(f)) {
        if (f->nparams != nparams)
            continue;
        same = 1;
        for (i = 0; i < nparams; i++)
            if (!type_equal(f->ptypes[i], ptypes[i]))
                same = 0;
        if (same)
            return f;
    }
    return 0;
}

/* overloaded functions get distinct IR names: scale(float), scale(vec3) */
void func_mangle_overloads(void)
{
    Symbol *f;
    char buf[512];
    int i;
    for (f = functions; f; f = f->scope_next) {
        if (func_first(f->name) == f && func_next(f) == 0)
            continue;               /* not overloaded */
        sprintf(buf, "%s(", f->name);
        for (i = 0; i < f->nparams; i++)
            sprintf(buf + strlen(buf), "%s%s", i ? "," : "", type_str(f->ptypes[i]));
        strcat(buf, ")");
        f->irname = strdup(buf);
    }
}

StructDef *struct_define(char *name, int line)
{
    StructDef *s = (StructDef *)calloc(1, sizeof(StructDef));
    s->name = name;
    s->line = line;
    if (structs_tail)
        structs_tail->next = s;
    else
        structs = s;
    structs_tail = s;
    return s;
}

StructDef *struct_lookup(const char *name)
{
    StructDef *s;
    for (s = structs; s; s = s->next)
        if (strcmp(s->name, name) == 0)
            return s;
    return 0;
}

int struct_member(StructDef *s, const char *member, Type *out)
{
    int i;
    for (i = 0; i < s->nmembers; i++)
        if (strcmp(s->mnames[i], member) == 0) {
            *out = s->mtypes[i];
            return 1;
        }
    return 0;
}

static char *kind_str(SymKind k)
{
    switch (k) {
    case SYM_VAR:     return "variable";
    case SYM_PARAM:   return "parameter";
    case SYM_FUNC:    return "function";
    case SYM_BUILTIN: return "built-in";
    }
    return "?";
}

void symtab_print(void)
{
    Symbol *s;
    StructDef *st;
    int i;
    char sig[256];

    if (structs) {
        printf("Struct types:\n");
        for (st = structs; st; st = st->next) {
            printf("  struct %s {", st->name);
            for (i = 0; i < st->nmembers; i++)
                printf(" %s %s;", type_str(st->mtypes[i]), st->mnames[i]);
            printf(" }   [line %d]\n", st->line);
        }
        printf("\n");
    }

    printf("%-16s %-10s %-22s %-18s %-12s %-6s %s\n",
           "NAME", "KIND", "TYPE", "QUALIFIERS", "SCOPE", "DEPTH", "LINE");
    printf("------------------------------------------------------------------------------------------------\n");
    for (s = sym_log; s; s = s->log_next) {
        if (s->kind == SYM_BUILTIN)
            continue;           /* the built-in table is long and fixed */
        if (s->kind == SYM_FUNC) {
            sprintf(sig, "%s(", type_str(s->type));
            for (i = 0; i < s->nparams; i++)
                sprintf(sig + strlen(sig), "%s%s", i ? "," : "", type_str(s->ptypes[i]));
            strcat(sig, ")");
            printf("%-16s %-10s %-22s %-18s %-12s %-6d %d%s\n", s->name, kind_str(s->kind),
                   sig, s->defined ? "defined" : "prototype", s->scope_name, s->depth, s->line, "");
        } else {
            printf("%-16s %-10s %-22s %-18s %-12s %-6d %d", s->name, kind_str(s->kind),
                   type_str(s->type), quals_str(s->quals), s->scope_name, s->depth, s->line);
            if (strcmp(s->irname, s->name) != 0)
                printf("   (IR name %s)", s->irname);
            if (s->has_const)
                printf("   (= %ld)", s->const_val);
            printf("\n");
        }
    }
}
