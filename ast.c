/*
 * ast.c -- AST construction helpers and the tree printer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "symtab.h"

Node *ast_root = 0;
int ast_typed = 0;      /* set once semantic analysis has annotated types */

Node *new_node(NodeKind kind, int line)
{
    Node *n = (Node *)calloc(1, sizeof(Node));
    n->kind = kind;
    n->line = line;
    n->type = error_type();
    return n;
}

Node *node1(NodeKind kind, int line, Node *a)
{
    Node *n = new_node(kind, line);
    n->a = a;
    return n;
}

Node *node2(NodeKind kind, int line, Node *a, Node *b)
{
    Node *n = new_node(kind, line);
    n->a = a;
    n->b = b;
    return n;
}

Node *node_op(NodeKind kind, int op, int line, Node *a, Node *b)
{
    Node *n = node2(kind, line, a, b);
    n->op = op;
    return n;
}

Node *node_str(NodeKind kind, int line, char *s)
{
    Node *n = new_node(kind, line);
    n->sval = s;
    return n;
}

Node *append(Node *list, Node *item)
{
    Node *p;
    if (list == 0)
        return item;
    if (item == 0)
        return list;
    for (p = list; p->next; p = p->next)
        ;
    p->next = item;
    return list;
}

int list_len(Node *list)
{
    int n = 0;
    for (; list; list = list->next)
        n++;
    return n;
}

char *op_str(int op)
{
    switch (op) {
    case OP_ADD:  return "+";   case OP_SUB:  return "-";
    case OP_MUL:  return "*";   case OP_DIV:  return "/";
    case OP_MOD:  return "%";   case OP_SHL:  return "<<";
    case OP_SHR:  return ">>";  case OP_LT:   return "<";
    case OP_GT:   return ">";   case OP_LE:   return "<=";
    case OP_GE:   return ">=";  case OP_EQ:   return "==";
    case OP_NE:   return "!=";  case OP_BAND: return "&";
    case OP_BXOR: return "^";   case OP_BOR:  return "|";
    case OP_LAND: return "&&";  case OP_LXOR: return "^^";
    case OP_LOR:  return "||";  case OP_NEG:  return "-";
    case OP_POS:  return "+";   case OP_NOT:  return "!";
    case OP_BNOT: return "~";   case OP_INC:  return "++";
    case OP_DEC:  return "--";  case OP_ASSIGN: return "=";
    }
    return "?";
}

char *quals_str(int q)
{
    static char buf[128];
    buf[0] = '\0';
    if (q & Q_CONST)   strcat(buf, "const ");
    if (q & Q_UNIFORM) strcat(buf, "uniform ");
    if (q & Q_BUFFER)  strcat(buf, "buffer ");
    if (q & Q_SHARED)  strcat(buf, "shared ");
    if (q & Q_IN)      strcat(buf, "in ");
    if (q & Q_OUT)     strcat(buf, "out ");
    if (q & Q_INOUT)   strcat(buf, "inout ");
    if (buf[0])
        buf[strlen(buf) - 1] = '\0';
    return buf;
}


/* ---------------- tree printer ----------------
 *
 * Draws the AST like the `tree` command:
 *
 *   Function void main()   [line 26]
 *   └── body: Block
 *       ├── DeclStmt
 *       │   └── VarDecl vec3 n   [line 28]
 *       │       └── init: Call normalize : vec3
 *       │           └── Id vNormal : vec3
 *
 * Unicode box-drawing characters by default, plain ASCII with --ascii.
 * ANSI colours only when ast_color is set (main.c: stdout is a terminal).
 */

int ast_ascii = 0;
int ast_color = 0;

#define C_KIND  "\033[1;36m"    /* node kind      : bold cyan */
#define C_TYPE  "\033[32m"      /* expression type: green     */
#define C_TAG   "\033[33m"      /* child role     : yellow    */
#define C_DIM   "\033[2m"       /* tree lines and line numbers */
#define C_OFF   "\033[0m"

static const char *col(const char *c)
{
    return ast_color ? c : "";
}

typedef struct {
    const char *tag;            /* role of the child: "cond", "then", ... (may be NULL) */
    Node *n;                    /* NULL is printed as "(none)" */
} Child;

typedef struct {
    Child *items;
    int n, cap;
} Children;

static void add(Children *c, const char *tag, Node *n)
{
    if (c->n == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 8;
        c->items = (Child *)realloc(c->items, c->cap * sizeof(Child));
    }
    c->items[c->n].tag = tag;
    c->items[c->n].n = n;
    c->n++;
}

static void add_list(Children *c, const char *tag, Node *list)
{
    for (; list; list = list->next)
        add(c, tag, list);
}

/* a DeclStmt holding one declaration is shown as just that declaration */
static Node *unwrap(Node *n)
{
    if (n && n->kind == N_DECLSTMT && n->a && n->a->next == 0)
        return n->a;
    return n;
}

static void add_stmts(Children *c, Node *list)
{
    for (; list; list = list->next)
        add(c, 0, unwrap(list));
}

/* struct and block members are printed as "Field" */
static void add_fields(Children *c, Node *list)
{
    for (; list; list = list->next) {
        if (list->kind == N_VARDECL)
            list->label = -1;
        add(c, 0, list);
    }
}

static int is_expr(Node *n)
{
    return n->kind >= N_ID && n->kind <= N_CONV && n->kind != N_INITLIST;
}

/* nodes that show the source line they come from */
static int shows_line(Node *n)
{
    switch (n->kind) {
    case N_FUNC: case N_PARAM: case N_VARDECL: case N_STRUCTDEF: case N_BLOCKDECL:
    case N_IF: case N_WHILE: case N_DOWHILE: case N_FOR: case N_SWITCH:
        return 1;
    default:
        return 0;
    }
}

/* the text of one node, without its children */
static void node_label(Node *n, char *out)
{
    char *q;
    Node *p;

    switch (n->kind) {
    case N_PROGRAM:   strcpy(out, "Program"); break;
    case N_FUNC:
        sprintf(out, "Function %s %s(", type_str(n->type), n->sval);
        for (p = n->a; p; p = p->next) {
            q = quals_str(p->quals);
            sprintf(out + strlen(out), "%s%s%s%s%s%s", p == n->a ? "" : ", ", q, *q ? " " : "",
                    type_str(p->type), p->sval ? " " : "", p->sval ? p->sval : "");
        }
        strcat(out, n->b ? ")" : ")  [prototype]");
        break;
    case N_PARAM: case N_VARDECL:
        q = quals_str(n->quals);
        sprintf(out, "%s %s%s%s%s%s %s",
                n->kind == N_PARAM ? "Param" : n->label == -1 ? "Field" : "VarDecl",
                n->layout ? n->layout : "", n->layout ? " " : "", q, *q ? " " : "",
                type_str(n->type), n->sval ? n->sval : "<unnamed>");
        if (n->sym && n->sym->irname && n->sval && strcmp(n->sym->irname, n->sval) != 0)
            sprintf(out + strlen(out), "  (IR name %s)", n->sym->irname);
        break;
    case N_STRUCTDEF: sprintf(out, "Struct %s", n->sval); break;
    case N_BLOCKDECL: sprintf(out, "InterfaceBlock %s %s", quals_str(n->quals), n->sval); break;
    case N_PRECISION: sprintf(out, "Precision %s %s", n->sval, type_str(n->type)); break;
    case N_IGNORED:   sprintf(out, "Declaration (%s)", n->sval ? n->sval : ""); break;
    case N_TYPESPEC:  sprintf(out, "TypeSpec %s", type_str(n->type)); break;

    case N_COMPOUND:  strcpy(out, "Block"); break;
    case N_DECLSTMT:  strcpy(out, "DeclStmt"); break;
    case N_EXPRSTMT:  strcpy(out, n->a ? "ExprStmt" : "EmptyStmt"); break;
    case N_IF:        strcpy(out, "If"); break;
    case N_WHILE:     strcpy(out, "While"); break;
    case N_DOWHILE:   strcpy(out, "DoWhile"); break;
    case N_FOR:       strcpy(out, "For"); break;
    case N_SWITCH:    strcpy(out, "Switch"); break;
    case N_CASE:      strcpy(out, "Case"); break;
    case N_DEFAULT:   strcpy(out, "Default"); break;
    case N_BREAK:     strcpy(out, "Break"); break;
    case N_CONTINUE:  strcpy(out, "Continue"); break;
    case N_RETURN:    strcpy(out, "Return"); break;
    case N_DISCARD:   strcpy(out, "Discard"); break;

    case N_ID:        sprintf(out, "Id %s", n->sval); break;
    case N_INTLIT: case N_UINTLIT: case N_FLOATLIT: case N_BOOLLIT:
                      sprintf(out, "Const %s", n->sval); break;
    case N_BINARY:    sprintf(out, "Binary %s", op_str(n->op)); break;
    case N_UNARY:     sprintf(out, "Unary %s", op_str(n->op)); break;
    case N_POSTFIX:   sprintf(out, "Postfix %s", op_str(n->op)); break;
    case N_ASSIGN:    sprintf(out, "Assign %s=", n->op == OP_ASSIGN ? "" : op_str(n->op)); break;
    case N_TERNARY:   strcpy(out, "Ternary ?:"); break;
    case N_INDEX:     strcpy(out, "Index []"); break;
    case N_FIELD:     sprintf(out, "%s .%s", n->is_swizzle ? "Swizzle" : "Member", n->sval); break;
    case N_CALL:      sprintf(out, "Call %s", n->sym && n->sym->irname ? n->sym->irname : n->sval); break;
    case N_METHOD:    sprintf(out, "Method .%s()", n->sval); break;
    case N_CTOR:      strcpy(out, "Construct"); break;
    case N_COMMA:     strcpy(out, "Comma"); break;
    case N_INITLIST:  strcpy(out, "InitList { }"); break;
    case N_CONV:      strcpy(out, "ImplicitConv to"); break;
    case N_ARRAYDIM:  strcpy(out, "ArrayDim"); break;
    default:          strcpy(out, "?"); break;
    }
}

/* the children of a node, each with its role */
static void node_children(Node *n, Children *c)
{
    switch (n->kind) {
    case N_PROGRAM:   add_list(c, 0, n->a); break;
    case N_FUNC:      if (n->b) add(c, "body", n->b); break;
    case N_VARDECL:   if (n->a) add(c, "init", n->a); break;
    case N_STRUCTDEF: add_fields(c, n->a); break;
    case N_BLOCKDECL:
        add_fields(c, n->a);
        if (n->b) add(c, "instance", n->b);
        break;
    case N_COMPOUND:
        add_stmts(c, n->a);
        break;
    case N_DECLSTMT:
        add_list(c, 0, n->a);
        break;
    case N_EXPRSTMT: case N_RETURN: case N_CASE:
        if (n->a) add(c, 0, n->a);
        break;
    case N_IF:
        add(c, "cond", n->a);
        add(c, "then", n->b);
        if (n->c) add(c, "else", n->c);
        break;
    case N_WHILE:
        add(c, "cond", n->a);
        add(c, "body", n->b);
        break;
    case N_DOWHILE:
        add(c, "body", n->a);
        add(c, "cond", n->b);
        break;
    case N_FOR:
        add(c, "init", unwrap(n->a));
        add(c, "cond", n->b);
        add(c, "step", n->c);
        add(c, "body", n->d);
        break;
    case N_SWITCH:
        add(c, "expr", n->a);
        add(c, "body", n->b);
        break;
    case N_BINARY: case N_COMMA:
        add(c, 0, n->a);
        add(c, 0, n->b);
        break;
    case N_ASSIGN:
        add(c, "target", n->a);
        add(c, "value", n->b);
        break;
    case N_TERNARY:
        add(c, "cond", n->a);
        add(c, "then", n->b);
        add(c, "else", n->c);
        break;
    case N_INDEX:
        add(c, 0, n->a);
        add(c, "index", n->b);
        break;
    case N_UNARY: case N_POSTFIX: case N_FIELD: case N_CONV: case N_ARRAYDIM:
        if (n->a) add(c, 0, n->a);
        break;
    case N_METHOD:
        add(c, 0, n->a);
        add_list(c, "arg", n->b);
        break;
    case N_CALL: case N_CTOR: case N_INITLIST:
        add_list(c, 0, n->a);
        break;
    default:
        break;
    }
}

static void print_tree(Node *n, const char *tag, const char *prefix, int last, int root)
{
    char label[1024], *child_prefix;
    Children kids = { 0, 0, 0 };
    int i;

    if (!root)
        printf("%s%s%s%s", col(C_DIM), prefix,
               ast_ascii ? (last ? "`-- " : "|-- ")
                         : (last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 "      /* └── */
                                 : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 "),    /* ├── */
               col(C_OFF));
    if (tag)
        printf("%s%s:%s ", col(C_TAG), tag, col(C_OFF));
    if (n == 0) {
        printf("%s(none)%s\n", col(C_DIM), col(C_OFF));
        return;
    }

    node_label(n, label);
    printf("%s%s%s", col(C_KIND), label, col(C_OFF));
    if (n->kind == N_CTOR || n->kind == N_CONV)
        printf(" %s%s%s", col(C_TYPE), type_str(n->type), col(C_OFF));   /* type is part of the node */
    else if (ast_typed && is_expr(n))
        printf(" : %s%s%s", col(C_TYPE), type_str(n->type), col(C_OFF));
    if (shows_line(n))
        printf("   %s[line %d]%s", col(C_DIM), n->line, col(C_OFF));
    printf("\n");

    node_children(n, &kids);
    child_prefix = (char *)malloc(strlen(prefix) + 16);
    strcpy(child_prefix, prefix);
    if (!root)
        strcat(child_prefix, last ? "    " : (ast_ascii ? "|   " : "\xe2\x94\x82   "));  /* │ */
    for (i = 0; i < kids.n; i++)
        print_tree(kids.items[i].n, kids.items[i].tag, child_prefix, i == kids.n - 1, 0);
    free(child_prefix);
    free(kids.items);
}

void print_ast(Node *n, int indent)
{
    (void)indent;
    if (n)
        print_tree(n, 0, "", 1, 1);
}
