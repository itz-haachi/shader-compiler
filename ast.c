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

/* ---------------- printer ---------------- */

static void pad(int indent)
{
    int i;
    for (i = 0; i < indent; i++)
        printf("  ");
}

static void print_list(Node *n, int indent)
{
    for (; n; n = n->next)
        print_ast(n, indent);
}

static void print_typed(Node *n)
{
    if (ast_typed)
        printf("  : %s", type_str(n->type));
    printf("\n");
}

static void print_decl_head(char *what, Node *n)
{
    char *q = quals_str(n->quals);
    printf("%s ", what);
    if (n->layout)
        printf("%s ", n->layout);
    if (*q)
        printf("%s ", q);
    printf("%s %s", type_str(n->type), n->sval ? n->sval : "<unnamed>");
    if (n->sym && n->sym->irname && strcmp(n->sym->irname, n->sval) != 0)
        printf("  (renamed %s)", n->sym->irname);
    printf("  [line %d]\n", n->line);
}

void print_ast(Node *n, int indent)
{
    if (n == 0)
        return;
    pad(indent);

    switch (n->kind) {
    case N_PROGRAM:
        printf("Program\n");
        print_list(n->a, indent + 1);
        return;
    case N_FUNC: {
        Node *p;
        printf("Function %s %s(", type_str(n->type), n->sval);
        for (p = n->a; p; p = p->next) {
            char *q = quals_str(p->quals);
            printf("%s%s%s%s%s%s", p == n->a ? "" : ", ", q, *q ? " " : "",
                   type_str(p->type), p->sval ? " " : "", p->sval ? p->sval : "");
        }
        printf(")%s  [line %d]\n", n->b ? "" : " prototype", n->line);
        if (n->b)
            print_ast(n->b, indent + 1);
        return;
    }
    case N_PARAM:
        print_decl_head("Param", n);
        return;
    case N_VARDECL:
        print_decl_head("VarDecl", n);
        if (n->a) {
            pad(indent + 1);
            printf("init\n");
            print_ast(n->a, indent + 2);
        }
        return;
    case N_STRUCTDEF:
        printf("Struct %s  [line %d]\n", n->sval, n->line);
        print_list(n->a, indent + 1);
        return;
    case N_BLOCKDECL:
        printf("InterfaceBlock %s %s  [line %d]\n", quals_str(n->quals), n->sval, n->line);
        print_list(n->a, indent + 1);
        if (n->b) {
            pad(indent + 1);
            printf("instance\n");
            print_ast(n->b, indent + 2);
        }
        return;
    case N_PRECISION:
        printf("Precision %s %s\n", n->sval, type_str(n->type));
        return;
    case N_IGNORED:
        printf("Declaration (%s)\n", n->sval ? n->sval : "qualifiers only");
        return;
    case N_TYPESPEC:
        printf("TypeSpec %s\n", type_str(n->type));
        return;

    case N_COMPOUND:
        printf("Block\n");
        print_list(n->a, indent + 1);
        return;
    case N_DECLSTMT:
        printf("DeclStmt\n");
        print_list(n->a, indent + 1);
        return;
    case N_EXPRSTMT:
        printf("ExprStmt\n");
        print_ast(n->a, indent + 1);
        return;
    case N_IF:
        printf("If  [line %d]\n", n->line);
        print_ast(n->a, indent + 1);
        pad(indent); printf("Then\n");
        print_ast(n->b, indent + 1);
        if (n->c) {
            pad(indent); printf("Else\n");
            print_ast(n->c, indent + 1);
        }
        return;
    case N_WHILE:
        printf("While  [line %d]\n", n->line);
        print_ast(n->a, indent + 1);
        print_ast(n->b, indent + 1);
        return;
    case N_DOWHILE:
        printf("DoWhile  [line %d]\n", n->line);
        print_ast(n->a, indent + 1);
        print_ast(n->b, indent + 1);
        return;
    case N_FOR:
        printf("For  [line %d]\n", n->line);
        pad(indent + 1); printf("init\n");  print_ast(n->a, indent + 2);
        pad(indent + 1); printf("cond\n");  print_ast(n->b, indent + 2);
        pad(indent + 1); printf("step\n");  print_ast(n->c, indent + 2);
        pad(indent + 1); printf("body\n");  print_ast(n->d, indent + 2);
        return;
    case N_SWITCH:
        printf("Switch  [line %d]\n", n->line);
        print_ast(n->a, indent + 1);
        print_ast(n->b, indent + 1);
        return;
    case N_CASE:
        printf("Case\n");
        print_ast(n->a, indent + 1);
        return;
    case N_DEFAULT:  printf("Default\n");  return;
    case N_BREAK:    printf("Break\n");    return;
    case N_CONTINUE: printf("Continue\n"); return;
    case N_DISCARD:  printf("Discard\n");  return;
    case N_RETURN:
        printf("Return\n");
        print_ast(n->a, indent + 1);
        return;

    case N_ID:
        printf("Id %s", n->sval);
        print_typed(n);
        return;
    case N_INTLIT: case N_UINTLIT: case N_FLOATLIT: case N_BOOLLIT:
        printf("Const %s", n->sval);
        print_typed(n);
        return;
    case N_BINARY:
        printf("Binary %s", op_str(n->op));
        print_typed(n);
        print_ast(n->a, indent + 1);
        print_ast(n->b, indent + 1);
        return;
    case N_UNARY:
        printf("Unary %s", op_str(n->op));
        print_typed(n);
        print_ast(n->a, indent + 1);
        return;
    case N_POSTFIX:
        printf("Postfix %s", op_str(n->op));
        print_typed(n);
        print_ast(n->a, indent + 1);
        return;
    case N_ASSIGN:
        printf("Assign %s%s", n->op == OP_ASSIGN ? "" : op_str(n->op), "=");
        print_typed(n);
        print_ast(n->a, indent + 1);
        print_ast(n->b, indent + 1);
        return;
    case N_TERNARY:
        printf("Ternary ?:");
        print_typed(n);
        print_ast(n->a, indent + 1);
        print_ast(n->b, indent + 1);
        print_ast(n->c, indent + 1);
        return;
    case N_INDEX:
        printf("Index []");
        print_typed(n);
        print_ast(n->a, indent + 1);
        print_ast(n->b, indent + 1);
        return;
    case N_FIELD:
        printf("%s .%s", n->is_swizzle ? "Swizzle" : "Field", n->sval);
        print_typed(n);
        print_ast(n->a, indent + 1);
        return;
    case N_CALL:
        printf("Call %s", n->sval);
        print_typed(n);
        print_list(n->a, indent + 1);
        return;
    case N_METHOD:
        printf("Method .%s()", n->sval);
        print_typed(n);
        print_ast(n->a, indent + 1);
        return;
    case N_CTOR:
        printf("Construct %s", type_str(n->type));
        if (ast_typed)
            printf("  : %s", type_str(n->type));
        printf("\n");
        print_list(n->a, indent + 1);
        return;
    case N_COMMA:
        printf("Comma");
        print_typed(n);
        print_ast(n->a, indent + 1);
        print_ast(n->b, indent + 1);
        return;
    case N_INITLIST:
        printf("InitList { }\n");
        print_list(n->a, indent + 1);
        return;
    case N_CONV:
        printf("ImplicitConv -> %s\n", type_str(n->type));
        print_ast(n->a, indent + 1);
        return;
    case N_ARRAYDIM:
        printf("ArrayDim\n");
        print_ast(n->a, indent + 1);
        return;
    }
    printf("?\n");
}
