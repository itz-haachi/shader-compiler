/*
 * main.c -- driver for the MiniGLSL compiler.
 *
 * Runs the phases in order and prints the output of each:
 *   1. Lexical analysis      (shader_final.l)  token stream
 *   2. Syntax analysis       (shader.y, ast.c) abstract syntax tree
 *   3. Semantic analysis     (semantic.c, symtab.c) symbol table + typed AST
 *   4. Intermediate code     (icg.c)           three-address code / quadruples
 *
 * A phase only runs if the earlier phases found no errors.
 *
 * Usage: ./minic [options] [file.frag]
 *   --no-tokens   do not print the token stream
 *   --no-ast      do not print the AST
 *   --no-symtab   do not print the symbol table
 *   --no-quads    do not print the quadruple table
 *   --ir-only     only print the three-address code (implies all of the above)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "symtab.h"
#include "semantic.h"
#include "icg.h"

extern FILE *yyin;
extern int yyparse(void);
extern int lex_errors, syntax_errors, show_tokens;
extern int n_globals, n_structs, n_functions;
extern char *functions[];

static void banner(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s [--no-tokens] [--no-ast] [--no-symtab] [--no-quads] [--ir-only] [file.frag]\n", prog);
}

int main(int argc, char *argv[])
{
    int i, result;
    int show_ast = 1, show_symtab = 1, show_quads = 1;
    char *file = 0;

    /* keep stdout and stderr (error messages) in order */
    setvbuf(stdout, 0, _IONBF, 0);

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-tokens") == 0)      show_tokens = 0;
        else if (strcmp(argv[i], "--no-ast") == 0)    show_ast = 0;
        else if (strcmp(argv[i], "--no-symtab") == 0) show_symtab = 0;
        else if (strcmp(argv[i], "--no-quads") == 0)  show_quads = 0;
        else if (strcmp(argv[i], "--ir-only") == 0)
            show_tokens = show_ast = show_symtab = show_quads = 0;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 2;
        } else {
            file = argv[i];
        }
    }

    if (file) {
        yyin = fopen(file, "r");
        if (yyin == NULL) {
            perror(file);
            return 1;
        }
    } else {
        printf("Enter shader code (Ctrl+D to finish):\n");
    }

    /* ---------- phases 1 + 2 ---------- */
    if (show_tokens) {
        banner("LEXICAL ANALYSIS");
        printf("%-5s %-12s %-14s %s\n", "LINE", "TOKEN", "DETAIL", "LEXEME");
        printf("----------------------------------------------\n");
    }

    result = yyparse();

    banner("SYNTAX ANALYSIS");
    if (result != 0 || lex_errors != 0 || syntax_errors != 0 || ast_root == 0) {
        printf("Invalid shader program (%d lexical, %d syntax error(s))\n",
               lex_errors, syntax_errors);
        return 1;
    }
    printf("Valid syntax\n");
    printf("Global declarations : %d\n", n_globals);
    printf("Structs defined     : %d\n", n_structs);
    printf("Functions defined   : %d", n_functions);
    for (i = 0; i < n_functions; i++)
        printf("%s %s()", i == 0 ? " ->" : ",", functions[i]);
    printf("\n");

    /* ---------- phase 3 ---------- */
    banner("SEMANTIC ANALYSIS");
    semantic_analyze(ast_root);

    if (show_symtab) {
        printf("\n---------- Symbol table ----------\n");
        symtab_print();
    }
    if (show_ast) {
        printf("\n---------- Typed abstract syntax tree ----------\n");
        print_ast(ast_root, 0);
    }
    printf("\n%d semantic error(s), %d warning(s)\n", semantic_errors, semantic_warnings);

    if (semantic_errors) {
        printf("\nInvalid shader program (%d semantic error(s))\n", semantic_errors);
        return 1;
    }

    /* ---------- phase 4 ---------- */
    banner("INTERMEDIATE CODE (three-address code)");
    icg_generate(ast_root);
    icg_print_tac();

    if (show_quads) {
        printf("\n---------- Quadruples ----------\n");
        icg_print_quads();
    }
    printf("\n---------- Temporaries ----------\n");
    icg_print_temps();

    printf("\nValid shader program (%d warning(s))\n", semantic_warnings);
    return 0;
}
