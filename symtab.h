/*
 * symtab.h -- scoped symbol table, function table and struct table.
 *
 * Scopes form a stack: depth 0 is the global scope, each function body,
 * block and for-loop pushes a new scope. Every symbol ever declared is
 * also kept in a log so the complete table can be printed at the end.
 */
#ifndef SYMTAB_H
#define SYMTAB_H

#include "types.h"

#define MAX_PARAMS 16
#define MAX_MEMBERS 64

typedef enum {
    SYM_VAR,        /* global or local variable  */
    SYM_PARAM,      /* function parameter        */
    SYM_FUNC,       /* user-defined function     */
    SYM_BUILTIN     /* built-in variable (gl_*)  */
} SymKind;

typedef struct Symbol {
    char *name;
    SymKind kind;
    Type type;              /* variable type / function return type */
    int quals;
    int line;
    int depth;              /* scope depth where declared */
    char *scope_name;       /* "global" or the enclosing function */
    char *irname;           /* unique name used in intermediate code */
    int readonly;           /* built-ins such as gl_FragCoord */

    /* functions */
    int nparams;
    Type ptypes[MAX_PARAMS];
    int pquals[MAX_PARAMS];
    int defined;            /* has a body (not just a prototype) */

    /* compile-time constant value (const int / uint) */
    int has_const;
    long const_val;

    struct Symbol *scope_next;
    struct Symbol *log_next;
} Symbol;

typedef struct StructDef {
    char *name;
    int nmembers;
    char *mnames[MAX_MEMBERS];
    Type mtypes[MAX_MEMBERS];
    int line;
    struct StructDef *next;
} StructDef;

void    symtab_init(void);
void    scope_push(void);
void    scope_pop(void);
int     scope_depth(void);
void    set_scope_name(char *name);

Symbol *sym_lookup(const char *name);
Symbol *sym_lookup_current(const char *name);
Symbol *sym_lookup_ir(const char *irname);
Symbol *sym_lookup_ir_scope(const char *irname, const char *scope_name);
Symbol *sym_insert(char *name, SymKind kind, Type type, int quals, int line);

Symbol *func_insert(char *name, Type ret, int nparams, Type *ptypes, int *pquals, int line);
Symbol *func_find_exact(const char *name, int nparams, Type *ptypes);
Symbol *func_first(const char *name);
Symbol *func_next(Symbol *f);       /* next overload with the same name */
void    func_mangle_overloads(void);

StructDef *struct_define(char *name, int line);
StructDef *struct_lookup(const char *name);
int        struct_member(StructDef *s, const char *member, Type *out);

void symtab_print(void);

#endif
