%{
/*
 * shader.y -- Syntax analyzer (yacc) for a GLSL-style shader language
 *
 * Phase 2 of the MiniGLSL compiler. The grammar is unchanged from
 * Review 1 (LALR(1), zero conflicts); every production now also builds
 * a node of the abstract syntax tree (see ast.h). The finished tree is
 * left in `ast_root` for the semantic analyzer (semantic.c) and the
 * intermediate code generator (icg.c). main() lives in main.c.
 *
 * Build:  make            (or see README.md for the manual commands)
 * Run:    ./minic test.frag
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

int yylex();
int yyerror(char *msg);

extern FILE *yyin;
extern int lineno;
extern int lex_errors;
extern char last_lexeme[];

int syntax_errors = 0;
int n_globals = 0;
int n_structs = 0;

/* names seen while parsing, for the summary */
char *functions[100];
int n_functions = 0;

/* user-defined struct names: the lexer returns these as TYPE_NAME */
char *struct_names[100];
int n_struct_names = 0;
static int n_anon = 0;

void add_struct_name(char *name)
{
    if (n_struct_names < 100)
        struct_names[n_struct_names++] = name;
}

int is_struct_name(char *name)
{
    int i;
    for (i = 0; i < n_struct_names; i++)
        if (strcmp(struct_names[i], name) == 0)
            return 1;
    return 0;
}

void add_function(char *name)
{
    if (n_functions < 100)
        functions[n_functions++] = name;
}

/* ---------- helpers that build declaration nodes ---------- */

static int qualifier_flag(char *q)
{
    if (strcmp(q, "const") == 0)     return Q_CONST;
    if (strcmp(q, "in") == 0)        return Q_IN;
    if (strcmp(q, "attribute") == 0) return Q_IN;
    if (strcmp(q, "out") == 0)       return Q_OUT;
    if (strcmp(q, "inout") == 0)     return Q_INOUT;
    if (strcmp(q, "uniform") == 0)   return Q_UNIFORM;
    if (strcmp(q, "buffer") == 0)    return Q_BUFFER;
    if (strcmp(q, "shared") == 0)    return Q_SHARED;
    return Q_OTHER;
}

static Node *make_spec(Type t)
{
    Node *n = new_node(N_TYPESPEC, lineno);
    n->type = t;
    return n;
}

static char *cat3(char *a, char *b, char *c)
{
    char *s = (char *)malloc(strlen(a) + strlen(b) + strlen(c) + 1);
    strcpy(s, a);
    strcat(s, b);
    strcat(s, c);
    return s;
}

/* copy a chain of N_ARRAYDIM nodes (they are shared between declarators) */
static Node *copy_dims(Node *d)
{
    Node *list = 0, *n;
    for (; d; d = d->next) {
        n = node1(N_ARRAYDIM, d->line, d->a);
        list = append(list, n);
    }
    return list;
}

/*
 * Combine "qualifiers type" (spec) with a list of declarators
 * (N_VARDECL nodes holding name, own array dims and initializer).
 * Returns a chain: [struct definition] declarator...
 */
static Node *finish_decl(Node *spec, Node *decls)
{
    Node *d, *result = 0;
    if (spec->a)                   /* struct S { ... } x; defines S too */
        result = spec->a;
    for (d = decls; d; d = d->next) {
        d->type = spec->type;
        d->quals = spec->quals;
        d->layout = spec->layout;
        d->d = append(copy_dims(spec->d), d->d);
    }
    return append(result, decls);
}
%}

%union {
    char *str;
    struct Node *node;
}

%token <str> IDENTIFIER TYPE_NAME QUALIFIER PREC_QUAL
%token <str> INTCONST UINTCONST FLOATCONST BOOLCONST
%token LAYOUT PRECISION STRUCT
%token IF ELSE FOR WHILE DO SWITCH CASE DEFAULT
%token BREAK CONTINUE RETURN DISCARD
%token INC DEC EQ NE LE GE AND OR XOR SHL SHR
%token ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN
%token AND_ASSIGN OR_ASSIGN XOR_ASSIGN SHL_ASSIGN SHR_ASSIGN

%type <node> program external_list external function_header param_list param
%type <node> declaration full_type qualifier_list qualifier type_spec struct_spec
%type <node> member_list member member_declarators array_spec
%type <node> init_declarator_list init_declarator initializer initializer_list
%type <node> compound_stmt stmt_list stmt expr_stmt selection_stmt iteration_stmt
%type <node> for_init opt_expr jump_stmt comma_expr expr primary arg_list
%type <str>  layout_list layout_item

/* dangling else: an ELSE belongs to the nearest IF */
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

/* operator precedence, lowest first (same as C / GLSL) */
%right '=' ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN AND_ASSIGN OR_ASSIGN XOR_ASSIGN SHL_ASSIGN SHR_ASSIGN
%right '?' ':'
%left OR
%left XOR
%left AND
%left '|'
%left '^'
%left '&'
%left EQ NE
%left '<' '>' LE GE
%left SHL SHR
%left '+' '-'
%left '*' '/' '%'
%right UNARY
%left INC DEC '[' '(' '.'

%%

program
    : external_list                   { ast_root = node1(N_PROGRAM, 1, $1); $$ = ast_root; }
    ;

external_list
    : external                        { $$ = $1; }
    | external_list external          { $$ = append($1, $2); }
    ;

external
    : function_header compound_stmt   { add_function($1->sval); $1->b = $2; $$ = $1; }
    | function_header ';'             { $$ = $1; }      /* prototype */
    | declaration                     { n_globals++; $$ = $1; }
    | error ';'                       { yyerrok; $$ = 0; }
    ;

function_header
    : full_type IDENTIFIER '(' ')'
        {
            $$ = node_str(N_FUNC, lineno, $2);
            $$->type = $1->type;
            $$->quals = $1->quals;
        }
    | full_type IDENTIFIER '(' param_list ')'
        {
            $$ = node_str(N_FUNC, lineno, $2);
            $$->type = $1->type;
            $$->quals = $1->quals;
            $$->a = $4;
        }
    ;

param_list
    : param                           { $$ = $1; }
    | param_list ',' param            { $$ = append($1, $3); }
    ;

param
    : full_type
        {
            $$ = new_node(N_PARAM, lineno);
            $$->type = $1->type; $$->quals = $1->quals; $$->d = $1->d;
        }
    | full_type IDENTIFIER
        {
            $$ = node_str(N_PARAM, lineno, $2);
            $$->type = $1->type; $$->quals = $1->quals; $$->d = $1->d;
        }
    | full_type IDENTIFIER array_spec
        {
            $$ = node_str(N_PARAM, lineno, $2);
            $$->type = $1->type; $$->quals = $1->quals;
            $$->d = append(copy_dims($1->d), $3);
        }
    ;

/* ---------------- Declarations ---------------- */

declaration
    : full_type ';'                              /* e.g. struct Light {...}; */
        {
            $$ = $1->a ? $1->a : node_str(N_IGNORED, lineno, "empty declaration");
        }
    | full_type init_declarator_list ';'         { $$ = finish_decl($1, $2); }
    | PRECISION PREC_QUAL TYPE_NAME ';'
        {
            $$ = node_str(N_PRECISION, lineno, $2);
            $$->type = type_from_name($3);
        }
    | qualifier_list ';'                         /* layout(local_size_x = 8) in; */
        {
            $$ = node_str(N_IGNORED, lineno, "qualifier-only declaration");
            $$->quals = $1->quals;
        }
    | qualifier_list IDENTIFIER ';'              /* invariant gl_Position; */
        {
            $$ = node_str(N_IGNORED, lineno, cat3("redeclaration of ", $2, ""));
            $$->quals = $1->quals;
        }
    | qualifier_list IDENTIFIER '{' member_list '}' ';'               /* interface block */
        {
            $$ = node_str(N_BLOCKDECL, lineno, $2);
            $$->quals = $1->quals; $$->layout = $1->layout; $$->a = $4;
        }
    | qualifier_list IDENTIFIER '{' member_list '}' IDENTIFIER ';'
        {
            $$ = node_str(N_BLOCKDECL, lineno, $2);
            $$->quals = $1->quals; $$->layout = $1->layout; $$->a = $4;
            $$->b = node_str(N_VARDECL, lineno, $6);
        }
    | qualifier_list IDENTIFIER '{' member_list '}' IDENTIFIER array_spec ';'
        {
            $$ = node_str(N_BLOCKDECL, lineno, $2);
            $$->quals = $1->quals; $$->layout = $1->layout; $$->a = $4;
            $$->b = node_str(N_VARDECL, lineno, $6);
            $$->b->d = $7;
        }
    ;

full_type
    : type_spec                       { $$ = $1; }
    | qualifier_list type_spec
        {
            $$ = $2;
            $$->quals = $1->quals;
            $$->layout = $1->layout;
        }
    ;

qualifier_list
    : qualifier                       { $$ = $1; }
    | qualifier_list qualifier
        {
            $$ = $1;
            $$->quals |= $2->quals;
            if ($2->layout)
                $$->layout = $$->layout ? cat3($$->layout, " ", $2->layout) : $2->layout;
        }
    ;

qualifier
    : QUALIFIER                       { $$ = make_spec(void_type()); $$->quals = qualifier_flag($1); }
    | PREC_QUAL                       { $$ = make_spec(void_type()); }
    | LAYOUT '(' layout_list ')'
        {
            $$ = make_spec(void_type());
            $$->quals = Q_LAYOUT;
            $$->layout = cat3("layout(", $3, ")");
        }
    ;

layout_list
    : layout_item                     { $$ = $1; }
    | layout_list ',' layout_item     { $$ = cat3($1, ", ", $3); }
    ;

layout_item
    : IDENTIFIER                      { $$ = $1; }
    | IDENTIFIER '=' expr
        {
            $$ = cat3($1, "=", ($3->kind == N_INTLIT || $3->kind == N_UINTLIT) ? $3->sval : "<expr>");
        }
    | QUALIFIER                       { $$ = $1; }   /* layout(shared) */
    ;

type_spec
    : TYPE_NAME                       { $$ = make_spec(type_from_name($1)); }
    | TYPE_NAME array_spec            { $$ = make_spec(type_from_name($1)); $$->d = $2; }
    | struct_spec                     { $$ = $1; }
    ;

struct_spec
    : STRUCT IDENTIFIER { add_struct_name($2); n_structs++; } '{' member_list '}'
        {
            $$ = make_spec(type_from_name($2));
            $$->a = node_str(N_STRUCTDEF, lineno, $2);
            $$->a->a = $5;
        }
    | STRUCT TYPE_NAME '{' member_list '}'      /* name already used: semantic error */
        {
            n_structs++;
            $$ = make_spec(type_from_name($2));
            $$->a = node_str(N_STRUCTDEF, lineno, $2);
            $$->a->a = $4;
        }
    | STRUCT '{' member_list '}'
        {
            char name[32];
            n_structs++;
            sprintf(name, "__anon%d", ++n_anon);
            $$ = make_spec(type_from_name(name));
            $$->a = node_str(N_STRUCTDEF, lineno, strdup(name));
            $$->a->a = $3;
        }
    ;

member_list
    : member                          { $$ = $1; }
    | member_list member              { $$ = append($1, $2); }
    ;

member
    : full_type member_declarators ';'   { $$ = finish_decl($1, $2); }
    ;

member_declarators
    : IDENTIFIER                      { $$ = node_str(N_VARDECL, lineno, $1); }
    | IDENTIFIER array_spec           { $$ = node_str(N_VARDECL, lineno, $1); $$->d = $2; }
    | member_declarators ',' IDENTIFIER
        {
            $$ = append($1, node_str(N_VARDECL, lineno, $3));
        }
    | member_declarators ',' IDENTIFIER array_spec
        {
            Node *m = node_str(N_VARDECL, lineno, $3);
            m->d = $4;
            $$ = append($1, m);
        }
    ;

array_spec
    : '[' ']'                         { $$ = node1(N_ARRAYDIM, lineno, 0); }
    | '[' expr ']'                    { $$ = node1(N_ARRAYDIM, lineno, $2); }
    | array_spec '[' ']'              { $$ = append($1, node1(N_ARRAYDIM, lineno, 0)); }
    | array_spec '[' expr ']'         { $$ = append($1, node1(N_ARRAYDIM, lineno, $3)); }
    ;

init_declarator_list
    : init_declarator                 { $$ = $1; }
    | init_declarator_list ',' init_declarator   { $$ = append($1, $3); }
    ;

init_declarator
    : IDENTIFIER                      { $$ = node_str(N_VARDECL, lineno, $1); }
    | IDENTIFIER array_spec           { $$ = node_str(N_VARDECL, lineno, $1); $$->d = $2; }
    | IDENTIFIER '=' initializer      { $$ = node_str(N_VARDECL, lineno, $1); $$->a = $3; }
    | IDENTIFIER array_spec '=' initializer
        {
            $$ = node_str(N_VARDECL, lineno, $1);
            $$->d = $2;
            $$->a = $4;
        }
    ;

initializer
    : expr                            { $$ = $1; }
    | '{' initializer_list '}'        { $$ = node1(N_INITLIST, lineno, $2); }
    | '{' initializer_list ',' '}'    { $$ = node1(N_INITLIST, lineno, $2); }
    ;

initializer_list
    : initializer                     { $$ = $1; }
    | initializer_list ',' initializer   { $$ = append($1, $3); }
    ;

/* ---------------- Statements ---------------- */

compound_stmt
    : '{' '}'                         { $$ = node1(N_COMPOUND, lineno, 0); }
    | '{' stmt_list '}'               { $$ = node1(N_COMPOUND, lineno, $2); }
    ;

stmt_list
    : stmt                            { $$ = $1; }
    | stmt_list stmt                  { $$ = append($1, $2); }
    ;

stmt
    : compound_stmt                   { $$ = $1; }
    | declaration                     { $$ = node1(N_DECLSTMT, lineno, $1); }
    | expr_stmt                       { $$ = $1; }
    | selection_stmt                  { $$ = $1; }
    | iteration_stmt                  { $$ = $1; }
    | jump_stmt                       { $$ = $1; }
    | CASE expr ':'                   { $$ = node1(N_CASE, lineno, $2); }
    | DEFAULT ':'                     { $$ = new_node(N_DEFAULT, lineno); }
    | error ';'                       { yyerrok; $$ = 0; }
    ;

expr_stmt
    : ';'                             { $$ = node1(N_EXPRSTMT, lineno, 0); }
    | comma_expr ';'                  { $$ = node1(N_EXPRSTMT, lineno, $1); }
    ;

selection_stmt
    : IF '(' comma_expr ')' stmt %prec LOWER_THAN_ELSE
        {
            $$ = node2(N_IF, $3->line, $3, $5);
        }
    | IF '(' comma_expr ')' stmt ELSE stmt
        {
            $$ = node2(N_IF, $3->line, $3, $5);
            $$->c = $7;
        }
    | SWITCH '(' comma_expr ')' compound_stmt   { $$ = node2(N_SWITCH, $3->line, $3, $5); }
    ;

iteration_stmt
    : WHILE '(' comma_expr ')' stmt   { $$ = node2(N_WHILE, $3->line, $3, $5); }
    | DO stmt WHILE '(' comma_expr ')' ';'
        {
            $$ = node2(N_DOWHILE, $5->line, $2, $5);
        }
    | FOR '(' for_init opt_expr ';' opt_expr ')' stmt
        {
            $$ = node2(N_FOR, $3 ? $3->line : lineno, $3, $4);
            $$->c = $6;
            $$->d = $8;
        }
    ;

for_init
    : expr_stmt                       { $$ = $1; }
    | declaration                     { $$ = node1(N_DECLSTMT, lineno, $1); }
    ;

opt_expr
    : /* empty */                     { $$ = 0; }
    | comma_expr                      { $$ = $1; }
    ;

jump_stmt
    : CONTINUE ';'                    { $$ = new_node(N_CONTINUE, lineno); }
    | BREAK ';'                       { $$ = new_node(N_BREAK, lineno); }
    | RETURN ';'                      { $$ = new_node(N_RETURN, lineno); }
    | RETURN comma_expr ';'           { $$ = node1(N_RETURN, lineno, $2); }
    | DISCARD ';'                     { $$ = new_node(N_DISCARD, lineno); }
    ;

/* ---------------- Expressions ---------------- */

comma_expr
    : expr                            { $$ = $1; }
    | comma_expr ',' expr             { $$ = node2(N_COMMA, lineno, $1, $3); }
    ;

expr
    : primary                         { $$ = $1; }
    | expr '[' comma_expr ']'         { $$ = node2(N_INDEX, lineno, $1, $3); }   /* array index */
    | expr '(' ')'                                                    /* call, e.g. a.length() */
        {
            if ($1->kind == N_FIELD) {
                $$ = node1(N_METHOD, lineno, $1->a);
                $$->sval = $1->sval;
            } else {
                $$ = node_str(N_CALL, lineno, $1->kind == N_ID ? $1->sval : "<expression>");
            }
        }
    | expr '(' arg_list ')'
        {
            if ($1->kind == N_FIELD) {
                $$ = node1(N_METHOD, lineno, $1->a);
                $$->sval = $1->sval;
                $$->b = $3;
            } else {
                $$ = node_str(N_CALL, lineno, $1->kind == N_ID ? $1->sval : "<expression>");
                $$->a = $3;
            }
        }
    | expr '.' IDENTIFIER             { $$ = node1(N_FIELD, lineno, $1); $$->sval = $3; }  /* member or swizzle: v.xyz */
    | expr INC                        { $$ = node_op(N_POSTFIX, OP_INC, lineno, $1, 0); }
    | expr DEC                        { $$ = node_op(N_POSTFIX, OP_DEC, lineno, $1, 0); }
    | INC expr                 %prec UNARY   { $$ = node_op(N_UNARY, OP_INC, lineno, $2, 0); }
    | DEC expr                 %prec UNARY   { $$ = node_op(N_UNARY, OP_DEC, lineno, $2, 0); }
    | '+' expr                 %prec UNARY   { $$ = node_op(N_UNARY, OP_POS, lineno, $2, 0); }
    | '-' expr                 %prec UNARY   { $$ = node_op(N_UNARY, OP_NEG, lineno, $2, 0); }
    | '!' expr                 %prec UNARY   { $$ = node_op(N_UNARY, OP_NOT, lineno, $2, 0); }
    | '~' expr                 %prec UNARY   { $$ = node_op(N_UNARY, OP_BNOT, lineno, $2, 0); }
    | expr '*' expr                   { $$ = node_op(N_BINARY, OP_MUL, lineno, $1, $3); }
    | expr '/' expr                   { $$ = node_op(N_BINARY, OP_DIV, lineno, $1, $3); }
    | expr '%' expr                   { $$ = node_op(N_BINARY, OP_MOD, lineno, $1, $3); }
    | expr '+' expr                   { $$ = node_op(N_BINARY, OP_ADD, lineno, $1, $3); }
    | expr '-' expr                   { $$ = node_op(N_BINARY, OP_SUB, lineno, $1, $3); }
    | expr SHL expr                   { $$ = node_op(N_BINARY, OP_SHL, lineno, $1, $3); }
    | expr SHR expr                   { $$ = node_op(N_BINARY, OP_SHR, lineno, $1, $3); }
    | expr '<' expr                   { $$ = node_op(N_BINARY, OP_LT, lineno, $1, $3); }
    | expr '>' expr                   { $$ = node_op(N_BINARY, OP_GT, lineno, $1, $3); }
    | expr LE expr                    { $$ = node_op(N_BINARY, OP_LE, lineno, $1, $3); }
    | expr GE expr                    { $$ = node_op(N_BINARY, OP_GE, lineno, $1, $3); }
    | expr EQ expr                    { $$ = node_op(N_BINARY, OP_EQ, lineno, $1, $3); }
    | expr NE expr                    { $$ = node_op(N_BINARY, OP_NE, lineno, $1, $3); }
    | expr '&' expr                   { $$ = node_op(N_BINARY, OP_BAND, lineno, $1, $3); }
    | expr '^' expr                   { $$ = node_op(N_BINARY, OP_BXOR, lineno, $1, $3); }
    | expr '|' expr                   { $$ = node_op(N_BINARY, OP_BOR, lineno, $1, $3); }
    | expr AND expr                   { $$ = node_op(N_BINARY, OP_LAND, lineno, $1, $3); }
    | expr XOR expr                   { $$ = node_op(N_BINARY, OP_LXOR, lineno, $1, $3); }
    | expr OR expr                    { $$ = node_op(N_BINARY, OP_LOR, lineno, $1, $3); }
    | expr '?' comma_expr ':' expr    { $$ = node2(N_TERNARY, lineno, $1, $3); $$->c = $5; }
    | expr '=' expr                   { $$ = node_op(N_ASSIGN, OP_ASSIGN, lineno, $1, $3); }
    | expr ADD_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_ADD, lineno, $1, $3); }
    | expr SUB_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_SUB, lineno, $1, $3); }
    | expr MUL_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_MUL, lineno, $1, $3); }
    | expr DIV_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_DIV, lineno, $1, $3); }
    | expr MOD_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_MOD, lineno, $1, $3); }
    | expr AND_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_BAND, lineno, $1, $3); }
    | expr OR_ASSIGN expr             { $$ = node_op(N_ASSIGN, OP_BOR, lineno, $1, $3); }
    | expr XOR_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_BXOR, lineno, $1, $3); }
    | expr SHL_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_SHL, lineno, $1, $3); }
    | expr SHR_ASSIGN expr            { $$ = node_op(N_ASSIGN, OP_SHR, lineno, $1, $3); }
    ;

primary
    : IDENTIFIER                      { $$ = node_str(N_ID, lineno, $1); }
    | INTCONST                        { $$ = node_str(N_INTLIT, lineno, $1); }
    | UINTCONST                       { $$ = node_str(N_UINTLIT, lineno, $1); }
    | FLOATCONST                      { $$ = node_str(N_FLOATLIT, lineno, $1); }
    | BOOLCONST                       { $$ = node_str(N_BOOLLIT, lineno, $1); }
    | '(' comma_expr ')'              { $$ = $2; }
    | TYPE_NAME '(' ')'                          /* constructor: vec4() */
        {
            $$ = new_node(N_CTOR, lineno);
            $$->type = type_from_name($1);
        }
    | TYPE_NAME '(' arg_list ')'                 /* constructor: vec4(c, 1.0) */
        {
            $$ = node1(N_CTOR, lineno, $3);
            $$->type = type_from_name($1);
        }
    | TYPE_NAME array_spec '(' arg_list ')'      /* array constructor: float[3](a, b, c) */
        {
            $$ = node1(N_CTOR, lineno, $4);
            $$->type = type_from_name($1);
            $$->d = $2;
        }
    ;

arg_list
    : expr                            { $$ = $1; }
    | arg_list ',' expr               { $$ = append($1, $3); }
    ;

%%

int yyerror(char *msg)
{
    fprintf(stderr, "line %d: syntax error near '%s'\n", lineno, last_lexeme);
    syntax_errors++;
    return 0;
}
