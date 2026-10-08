%{
/*
 * shader.y -- Syntax analyzer (yacc) for a GLSL-style shader language
 *
 * Build:  lex shader.l
 *         yacc -d shader.y
 *         gcc lex.yy.c y.tab.c -w
 * Run:    ./a.out test.frag          (or ./a.out, then type and press Ctrl+D)
 *
 * The lexer prints each token; the parser checks that the token stream
 * forms a valid shader and prints a summary at the end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
%}

%union {
    char *str;
}

%token <str> IDENTIFIER TYPE_NAME QUALIFIER PREC_QUAL
%token INTCONST UINTCONST FLOATCONST BOOLCONST
%token LAYOUT PRECISION STRUCT
%token IF ELSE FOR WHILE DO SWITCH CASE DEFAULT
%token BREAK CONTINUE RETURN DISCARD
%token INC DEC EQ NE LE GE AND OR XOR SHL SHR
%token ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN
%token AND_ASSIGN OR_ASSIGN XOR_ASSIGN SHL_ASSIGN SHR_ASSIGN

%type <str> function_header

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
    : external_list
    ;

external_list
    : external
    | external_list external
    ;

external
    : function_header compound_stmt   { add_function($1); }
    | function_header ';'             /* prototype */
    | declaration                     { n_globals++; }
    ;

function_header
    : full_type IDENTIFIER '(' ')'              { $$ = $2; }
    | full_type IDENTIFIER '(' param_list ')'   { $$ = $2; }
    ;

param_list
    : param
    | param_list ',' param
    ;

param
    : full_type
    | full_type IDENTIFIER
    | full_type IDENTIFIER array_spec
    ;

/* ---------------- Declarations ---------------- */

declaration
    : full_type ';'                              /* e.g. struct Light {...}; */
    | full_type init_declarator_list ';'
    | PRECISION PREC_QUAL TYPE_NAME ';'
    | qualifier_list ';'                         /* layout(local_size_x = 8) in; */
    | qualifier_list IDENTIFIER ';'              /* invariant gl_Position; */
    | qualifier_list IDENTIFIER '{' member_list '}' ';'               /* interface block */
    | qualifier_list IDENTIFIER '{' member_list '}' IDENTIFIER ';'
    | qualifier_list IDENTIFIER '{' member_list '}' IDENTIFIER array_spec ';'
    ;

full_type
    : type_spec
    | qualifier_list type_spec
    ;

qualifier_list
    : qualifier
    | qualifier_list qualifier
    ;

qualifier
    : QUALIFIER
    | PREC_QUAL
    | LAYOUT '(' layout_list ')'
    ;

layout_list
    : layout_item
    | layout_list ',' layout_item
    ;

layout_item
    : IDENTIFIER
    | IDENTIFIER '=' expr
    | QUALIFIER                                  /* layout(shared) */
    ;

type_spec
    : TYPE_NAME
    | TYPE_NAME array_spec
    | struct_spec
    ;

struct_spec
    : STRUCT IDENTIFIER { add_struct_name($2); n_structs++; } '{' member_list '}'
    | STRUCT '{' member_list '}'                 { n_structs++; }
    ;

member_list
    : member
    | member_list member
    ;

member
    : full_type member_declarators ';'
    ;

member_declarators
    : IDENTIFIER
    | IDENTIFIER array_spec
    | member_declarators ',' IDENTIFIER
    | member_declarators ',' IDENTIFIER array_spec
    ;

array_spec
    : '[' ']'
    | '[' expr ']'
    | array_spec '[' ']'
    | array_spec '[' expr ']'
    ;

init_declarator_list
    : init_declarator
    | init_declarator_list ',' init_declarator
    ;

init_declarator
    : IDENTIFIER
    | IDENTIFIER array_spec
    | IDENTIFIER '=' initializer
    | IDENTIFIER array_spec '=' initializer
    ;

initializer
    : expr
    | '{' initializer_list '}'
    | '{' initializer_list ',' '}'
    ;

initializer_list
    : initializer
    | initializer_list ',' initializer
    ;

/* ---------------- Statements ---------------- */

compound_stmt
    : '{' '}'
    | '{' stmt_list '}'
    ;

stmt_list
    : stmt
    | stmt_list stmt
    ;

stmt
    : compound_stmt
    | declaration
    | expr_stmt
    | selection_stmt
    | iteration_stmt
    | jump_stmt
    | CASE expr ':'
    | DEFAULT ':'
    ;

expr_stmt
    : ';'
    | comma_expr ';'
    ;

selection_stmt
    : IF '(' comma_expr ')' stmt %prec LOWER_THAN_ELSE
    | IF '(' comma_expr ')' stmt ELSE stmt
    | SWITCH '(' comma_expr ')' compound_stmt
    ;

iteration_stmt
    : WHILE '(' comma_expr ')' stmt
    | DO stmt WHILE '(' comma_expr ')' ';'
    | FOR '(' for_init opt_expr ';' opt_expr ')' stmt
    ;

for_init
    : expr_stmt
    | declaration
    ;

opt_expr
    : /* empty */
    | comma_expr
    ;

jump_stmt
    : CONTINUE ';'
    | BREAK ';'
    | RETURN ';'
    | RETURN comma_expr ';'
    | DISCARD ';'
    ;

/* ---------------- Expressions ---------------- */

comma_expr
    : expr
    | comma_expr ',' expr
    ;

expr
    : primary
    | expr '[' comma_expr ']'                    /* array index */
    | expr '(' ')'                               /* call, e.g. a.length() */
    | expr '(' arg_list ')'
    | expr '.' IDENTIFIER                        /* member or swizzle: v.xyz */
    | expr INC
    | expr DEC
    | INC expr                 %prec UNARY
    | DEC expr                 %prec UNARY
    | '+' expr                 %prec UNARY
    | '-' expr                 %prec UNARY
    | '!' expr                 %prec UNARY
    | '~' expr                 %prec UNARY
    | expr '*' expr
    | expr '/' expr
    | expr '%' expr
    | expr '+' expr
    | expr '-' expr
    | expr SHL expr
    | expr SHR expr
    | expr '<' expr
    | expr '>' expr
    | expr LE expr
    | expr GE expr
    | expr EQ expr
    | expr NE expr
    | expr '&' expr
    | expr '^' expr
    | expr '|' expr
    | expr AND expr
    | expr XOR expr
    | expr OR expr
    | expr '?' comma_expr ':' expr
    | expr '=' expr
    | expr ADD_ASSIGN expr
    | expr SUB_ASSIGN expr
    | expr MUL_ASSIGN expr
    | expr DIV_ASSIGN expr
    | expr MOD_ASSIGN expr
    | expr AND_ASSIGN expr
    | expr OR_ASSIGN expr
    | expr XOR_ASSIGN expr
    | expr SHL_ASSIGN expr
    | expr SHR_ASSIGN expr
    ;

primary
    : IDENTIFIER
    | INTCONST
    | UINTCONST
    | FLOATCONST
    | BOOLCONST
    | '(' comma_expr ')'
    | TYPE_NAME '(' ')'                          /* constructor: vec4() */
    | TYPE_NAME '(' arg_list ')'                 /* constructor: vec4(c, 1.0) */
    ;

arg_list
    : expr
    | arg_list ',' expr
    ;

%%

int yyerror(char *msg)
{
    fprintf(stderr, "line %d: syntax error near '%s'\n", lineno, last_lexeme);
    syntax_errors++;
    return 0;
}

int main(int argc, char *argv[])
{
    int i, result;

    if (argc > 1) {
        yyin = fopen(argv[1], "r");
        if (yyin == NULL) {
            perror(argv[1]);
            return 1;
        }
    } else {
        printf("Enter shader code (Ctrl+D to finish):\n");
    }

    printf("\n========== LEXICAL ANALYSIS ==========\n");
    printf("%-5s %-12s %-14s %s\n", "LINE", "TOKEN", "DETAIL", "LEXEME");
    printf("----------------------------------------------\n");

    result = yyparse();

    printf("\n========== SYNTAX ANALYSIS ==========\n");
    if (result == 0 && lex_errors == 0 && syntax_errors == 0) {
        printf("Valid shader program\n");
        printf("Global declarations : %d\n", n_globals);
        printf("Structs defined     : %d\n", n_structs);
        printf("Functions defined   : %d", n_functions);
        for (i = 0; i < n_functions; i++)
            printf("%s %s()", i == 0 ? " ->" : ",", functions[i]);
        printf("\n");
        return 0;
    }

    printf("Invalid shader program (%d lexical, %d syntax error(s))\n",
           lex_errors, syntax_errors);
    return 1;
}
