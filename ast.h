/*
 * ast.h -- abstract syntax tree built by the parser (shader.y).
 *
 * The semantic analyzer annotates every expression node with its type
 * (node->type) and inserts explicit N_CONV nodes for implicit
 * conversions, giving the "typed AST" that the intermediate code
 * generator walks.
 */
#ifndef AST_H
#define AST_H

#include "types.h"

typedef enum {
    /* top level and declarations */
    N_PROGRAM,      /* a = list of externals                               */
    N_FUNC,         /* sval = name, type = return type, a = params, b = body */
    N_PARAM,        /* sval = name (may be NULL), type, quals, d = array dims */
    N_VARDECL,      /* sval = name, type, quals, a = initializer, d = dims  */
    N_STRUCTDEF,    /* sval = struct name, a = member list (N_VARDECL)      */
    N_BLOCKDECL,    /* interface block: sval = block name, a = members,
                       b = instance N_VARDECL (or NULL)                     */
    N_TYPESPEC,     /* temporary node: full type + qualifiers while parsing */
    N_PRECISION,    /* precision mediump float;                             */
    N_IGNORED,      /* layout(...) in; / invariant gl_Position;             */

    /* statements */
    N_COMPOUND,     /* a = statement list                                   */
    N_DECLSTMT,     /* a = list of N_VARDECL / N_STRUCTDEF                  */
    N_EXPRSTMT,     /* a = expression (NULL for ';')                        */
    N_IF,           /* a = cond, b = then, c = else                         */
    N_WHILE,        /* a = cond, b = body                                   */
    N_DOWHILE,      /* a = body, b = cond                                   */
    N_FOR,          /* a = init stmt, b = cond, c = step, d = body          */
    N_SWITCH,       /* a = expr, b = body                                   */
    N_CASE,         /* a = label expr                                       */
    N_DEFAULT,
    N_BREAK,
    N_CONTINUE,
    N_RETURN,       /* a = value (may be NULL)                              */
    N_DISCARD,

    /* expressions */
    N_ID,           /* sval = name                                          */
    N_INTLIT,       /* sval = text                                          */
    N_UINTLIT,
    N_FLOATLIT,
    N_BOOLLIT,
    N_BINARY,       /* op, a, b                                             */
    N_UNARY,        /* op (NEG, POS, NOT, BNOT, INC, DEC = prefix), a       */
    N_POSTFIX,      /* op (INC, DEC), a                                     */
    N_ASSIGN,       /* op (OP_ASSIGN or the arithmetic op of +=, ...), a, b */
    N_TERNARY,      /* a ? b : c                                            */
    N_INDEX,        /* a[b]                                                 */
    N_FIELD,        /* a.sval -- struct member or swizzle (is_swizzle)      */
    N_CALL,         /* sval = function name, a = argument list              */
    N_METHOD,       /* a.sval() -- only arr.length() is supported           */
    N_CTOR,         /* type = constructed type, a = argument list           */
    N_COMMA,        /* a, b                                                 */
    N_INITLIST,     /* { a, ... }                                           */
    N_CONV,         /* implicit conversion of a to type (added by semantic) */
    N_ARRAYDIM      /* [a] in a declaration (a may be NULL)                 */
} NodeKind;

/* operator codes stored in Node.op */
enum {
    OP_NONE = 0,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_SHL, OP_SHR,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NE,
    OP_BAND, OP_BXOR, OP_BOR,
    OP_LAND, OP_LXOR, OP_LOR,
    OP_NEG, OP_POS, OP_NOT, OP_BNOT,
    OP_INC, OP_DEC,
    OP_ASSIGN
};

/* qualifier flags stored in Node.quals and Symbol.quals */
#define Q_CONST     0x001
#define Q_IN        0x002
#define Q_OUT       0x004
#define Q_INOUT     0x008
#define Q_UNIFORM   0x010
#define Q_BUFFER    0x020
#define Q_SHARED    0x040
#define Q_OTHER     0x080   /* flat, smooth, invariant, ... (no semantic effect here) */
#define Q_LAYOUT    0x100

struct Symbol;

typedef struct Node {
    NodeKind kind;
    int line;
    char *sval;
    int op;
    int quals;
    char *layout;           /* text of layout(...) if any */
    struct Node *a, *b, *c, *d;
    struct Node *next;      /* sibling in a list */
    Type type;
    struct Symbol *sym;     /* resolved symbol (identifiers, declarations) */
    int lvalue;
    int is_swizzle;
    int label;              /* used by the code generator for case labels */
} Node;

extern Node *ast_root;

Node *new_node(NodeKind kind, int line);
Node *node1(NodeKind kind, int line, Node *a);
Node *node2(NodeKind kind, int line, Node *a, Node *b);
Node *node_op(NodeKind kind, int op, int line, Node *a, Node *b);
Node *node_str(NodeKind kind, int line, char *s);
Node *append(Node *list, Node *item);   /* item may itself be a chain */
int   list_len(Node *list);

char *op_str(int op);
char *quals_str(int quals);
void  print_ast(Node *n, int indent);

#endif
