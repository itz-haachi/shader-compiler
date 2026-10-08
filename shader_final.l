%{
/*
 * shader.l -- Lexical analyzer for a GLSL-style shader language
 *
 * Works together with shader.y (the yacc parser).
 *
 * Build:  lex shader.l
 *         yacc -d shader.y
 *         gcc lex.yy.c y.tab.c -w
 * Run:    ./a.out test.frag
 *
 * Every token is printed as it is recognised (the lexical analysis
 * output) and then returned to the parser.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "y.tab.h"

int lineno = 1;
int lex_errors = 0;
char last_lexeme[256] = "";     /* used by yyerror() in shader.y */

int is_struct_name(char *s);    /* defined in shader.y */
void skip_block_comment();

/* ---------------- Keyword table ---------------- */
#define RESERVED (-1)

struct keyword {
    char *name;
    int token;
    char *category;
};

struct keyword keywords[] = {
    /* storage and interface qualifiers */
    {"const", QUALIFIER, "qualifier"},      {"uniform", QUALIFIER, "qualifier"},
    {"buffer", QUALIFIER, "qualifier"},     {"shared", QUALIFIER, "qualifier"},
    {"attribute", QUALIFIER, "qualifier"},  {"varying", QUALIFIER, "qualifier"},
    {"in", QUALIFIER, "qualifier"},         {"out", QUALIFIER, "qualifier"},
    {"inout", QUALIFIER, "qualifier"},      {"centroid", QUALIFIER, "qualifier"},
    {"flat", QUALIFIER, "qualifier"},       {"smooth", QUALIFIER, "qualifier"},
    {"noperspective", QUALIFIER, "qualifier"}, {"patch", QUALIFIER, "qualifier"},
    {"sample", QUALIFIER, "qualifier"},     {"invariant", QUALIFIER, "qualifier"},
    {"precise", QUALIFIER, "qualifier"},    {"coherent", QUALIFIER, "qualifier"},
    {"volatile", QUALIFIER, "qualifier"},   {"restrict", QUALIFIER, "qualifier"},
    {"readonly", QUALIFIER, "qualifier"},   {"writeonly", QUALIFIER, "qualifier"},
    {"subroutine", QUALIFIER, "qualifier"},
    {"layout", LAYOUT, "qualifier"},

    /* precision */
    {"precision", PRECISION, "precision"},
    {"highp", PREC_QUAL, "precision"},
    {"mediump", PREC_QUAL, "precision"},
    {"lowp", PREC_QUAL, "precision"},

    /* control flow */
    {"if", IF, "control"},           {"else", ELSE, "control"},
    {"for", FOR, "control"},         {"while", WHILE, "control"},
    {"do", DO, "control"},           {"switch", SWITCH, "control"},
    {"case", CASE, "control"},       {"default", DEFAULT, "control"},
    {"break", BREAK, "control"},     {"continue", CONTINUE, "control"},
    {"return", RETURN, "control"},   {"discard", DISCARD, "control"},

    {"struct", STRUCT, "type"},

    /* scalar types */
    {"void", TYPE_NAME, "type"},   {"bool", TYPE_NAME, "type"},
    {"int", TYPE_NAME, "type"},    {"uint", TYPE_NAME, "type"},
    {"float", TYPE_NAME, "type"},  {"double", TYPE_NAME, "type"},
    {"atomic_uint", TYPE_NAME, "type"},

    /* vector types */
    {"vec2", TYPE_NAME, "type"},  {"vec3", TYPE_NAME, "type"},  {"vec4", TYPE_NAME, "type"},
    {"ivec2", TYPE_NAME, "type"}, {"ivec3", TYPE_NAME, "type"}, {"ivec4", TYPE_NAME, "type"},
    {"uvec2", TYPE_NAME, "type"}, {"uvec3", TYPE_NAME, "type"}, {"uvec4", TYPE_NAME, "type"},
    {"bvec2", TYPE_NAME, "type"}, {"bvec3", TYPE_NAME, "type"}, {"bvec4", TYPE_NAME, "type"},
    {"dvec2", TYPE_NAME, "type"}, {"dvec3", TYPE_NAME, "type"}, {"dvec4", TYPE_NAME, "type"},

    /* matrix types */
    {"mat2", TYPE_NAME, "type"},   {"mat3", TYPE_NAME, "type"},   {"mat4", TYPE_NAME, "type"},
    {"mat2x2", TYPE_NAME, "type"}, {"mat2x3", TYPE_NAME, "type"}, {"mat2x4", TYPE_NAME, "type"},
    {"mat3x2", TYPE_NAME, "type"}, {"mat3x3", TYPE_NAME, "type"}, {"mat3x4", TYPE_NAME, "type"},
    {"mat4x2", TYPE_NAME, "type"}, {"mat4x3", TYPE_NAME, "type"}, {"mat4x4", TYPE_NAME, "type"},
    {"dmat2", TYPE_NAME, "type"},   {"dmat3", TYPE_NAME, "type"},   {"dmat4", TYPE_NAME, "type"},
    {"dmat2x2", TYPE_NAME, "type"}, {"dmat2x3", TYPE_NAME, "type"}, {"dmat2x4", TYPE_NAME, "type"},
    {"dmat3x2", TYPE_NAME, "type"}, {"dmat3x3", TYPE_NAME, "type"}, {"dmat3x4", TYPE_NAME, "type"},
    {"dmat4x2", TYPE_NAME, "type"}, {"dmat4x3", TYPE_NAME, "type"}, {"dmat4x4", TYPE_NAME, "type"},

    /* sampler types */
    {"sampler1D", TYPE_NAME, "type"},         {"sampler1DShadow", TYPE_NAME, "type"},
    {"sampler1DArray", TYPE_NAME, "type"},    {"sampler1DArrayShadow", TYPE_NAME, "type"},
    {"isampler1D", TYPE_NAME, "type"},        {"isampler1DArray", TYPE_NAME, "type"},
    {"usampler1D", TYPE_NAME, "type"},        {"usampler1DArray", TYPE_NAME, "type"},
    {"sampler2D", TYPE_NAME, "type"},         {"sampler2DShadow", TYPE_NAME, "type"},
    {"sampler2DArray", TYPE_NAME, "type"},    {"sampler2DArrayShadow", TYPE_NAME, "type"},
    {"isampler2D", TYPE_NAME, "type"},        {"isampler2DArray", TYPE_NAME, "type"},
    {"usampler2D", TYPE_NAME, "type"},        {"usampler2DArray", TYPE_NAME, "type"},
    {"sampler2DRect", TYPE_NAME, "type"},     {"sampler2DRectShadow", TYPE_NAME, "type"},
    {"isampler2DRect", TYPE_NAME, "type"},    {"usampler2DRect", TYPE_NAME, "type"},
    {"sampler2DMS", TYPE_NAME, "type"},       {"isampler2DMS", TYPE_NAME, "type"},
    {"usampler2DMS", TYPE_NAME, "type"},      {"sampler2DMSArray", TYPE_NAME, "type"},
    {"isampler2DMSArray", TYPE_NAME, "type"}, {"usampler2DMSArray", TYPE_NAME, "type"},
    {"sampler3D", TYPE_NAME, "type"},         {"isampler3D", TYPE_NAME, "type"},
    {"usampler3D", TYPE_NAME, "type"},
    {"samplerCube", TYPE_NAME, "type"},       {"samplerCubeShadow", TYPE_NAME, "type"},
    {"isamplerCube", TYPE_NAME, "type"},      {"usamplerCube", TYPE_NAME, "type"},
    {"samplerCubeArray", TYPE_NAME, "type"},  {"samplerCubeArrayShadow", TYPE_NAME, "type"},
    {"isamplerCubeArray", TYPE_NAME, "type"}, {"usamplerCubeArray", TYPE_NAME, "type"},
    {"samplerBuffer", TYPE_NAME, "type"},     {"isamplerBuffer", TYPE_NAME, "type"},
    {"usamplerBuffer", TYPE_NAME, "type"},

    /* image types */
    {"image1D", TYPE_NAME, "type"},        {"iimage1D", TYPE_NAME, "type"},
    {"uimage1D", TYPE_NAME, "type"},       {"image1DArray", TYPE_NAME, "type"},
    {"iimage1DArray", TYPE_NAME, "type"},  {"uimage1DArray", TYPE_NAME, "type"},
    {"image2D", TYPE_NAME, "type"},        {"iimage2D", TYPE_NAME, "type"},
    {"uimage2D", TYPE_NAME, "type"},       {"image2DArray", TYPE_NAME, "type"},
    {"iimage2DArray", TYPE_NAME, "type"},  {"uimage2DArray", TYPE_NAME, "type"},
    {"image2DRect", TYPE_NAME, "type"},    {"iimage2DRect", TYPE_NAME, "type"},
    {"uimage2DRect", TYPE_NAME, "type"},   {"image2DMS", TYPE_NAME, "type"},
    {"iimage2DMS", TYPE_NAME, "type"},     {"uimage2DMS", TYPE_NAME, "type"},
    {"image2DMSArray", TYPE_NAME, "type"}, {"iimage2DMSArray", TYPE_NAME, "type"},
    {"uimage2DMSArray", TYPE_NAME, "type"},
    {"image3D", TYPE_NAME, "type"},        {"iimage3D", TYPE_NAME, "type"},
    {"uimage3D", TYPE_NAME, "type"},       {"imageCube", TYPE_NAME, "type"},
    {"iimageCube", TYPE_NAME, "type"},     {"uimageCube", TYPE_NAME, "type"},
    {"imageCubeArray", TYPE_NAME, "type"}, {"iimageCubeArray", TYPE_NAME, "type"},
    {"uimageCubeArray", TYPE_NAME, "type"},
    {"imageBuffer", TYPE_NAME, "type"},    {"iimageBuffer", TYPE_NAME, "type"},
    {"uimageBuffer", TYPE_NAME, "type"},

    /* boolean constants */
    {"true", BOOLCONST, "bool"},  {"false", BOOLCONST, "bool"},

    /* reserved for future use: using one is an error */
    {"common", RESERVED, 0},    {"partition", RESERVED, 0}, {"active", RESERVED, 0},
    {"asm", RESERVED, 0},       {"class", RESERVED, 0},     {"union", RESERVED, 0},
    {"enum", RESERVED, 0},      {"typedef", RESERVED, 0},   {"template", RESERVED, 0},
    {"this", RESERVED, 0},      {"resource", RESERVED, 0},  {"goto", RESERVED, 0},
    {"inline", RESERVED, 0},    {"noinline", RESERVED, 0},  {"public", RESERVED, 0},
    {"static", RESERVED, 0},    {"extern", RESERVED, 0},    {"external", RESERVED, 0},
    {"interface", RESERVED, 0}, {"long", RESERVED, 0},      {"short", RESERVED, 0},
    {"half", RESERVED, 0},      {"fixed", RESERVED, 0},     {"unsigned", RESERVED, 0},
    {"superp", RESERVED, 0},    {"input", RESERVED, 0},     {"output", RESERVED, 0},
    {"hvec2", RESERVED, 0},     {"hvec3", RESERVED, 0},     {"hvec4", RESERVED, 0},
    {"fvec2", RESERVED, 0},     {"fvec3", RESERVED, 0},     {"fvec4", RESERVED, 0},
    {"sampler3DRect", RESERVED, 0}, {"filter", RESERVED, 0}, {"sizeof", RESERVED, 0},
    {"cast", RESERVED, 0},      {"namespace", RESERVED, 0}, {"using", RESERVED, 0},

    {0, 0, 0}
};

struct keyword *lookup_keyword(char *s)
{
    int i;
    for (i = 0; keywords[i].name != 0; i++)
        if (strcmp(keywords[i].name, s) == 0)
            return &keywords[i];
    return 0;
}

/* print the token, remember it for error messages, and return it */
int emit(int tok, char *cls, char *detail)
{
    printf("%-5d %-12s %-14s %s\n", lineno, cls, detail ? detail : "", yytext);
    strncpy(last_lexeme, yytext, sizeof(last_lexeme) - 1);
    return tok;
}

void lex_error(char *msg)
{
    fprintf(stderr, "line %d: lexical error: %s '%s'\n", lineno, msg, yytext);
    lex_errors++;
}
%}

D       [0-9]
NZ      [1-9]
O       [0-7]
H       [0-9a-fA-F]
L       [a-zA-Z_]
E       ([eE][+-]?[0-9]+)
FS      ([fF]|lf|LF)

%%

[ \t\r\f\v]+                    ;
\n                              { lineno++; }
"//".*                          ;
"/*"                            { skip_block_comment(); }
^[ \t]*"#".*                    { printf("%-5d %-12s %-14s %s\n", lineno, "PREPROCESSOR", "", yytext); }

{D}+"."{D}*{E}?{FS}?            { return emit(FLOATCONST, "CONSTANT", "float"); }
"."{D}+{E}?{FS}?                { return emit(FLOATCONST, "CONSTANT", "float"); }
{D}+{E}{FS}?                    { return emit(FLOATCONST, "CONSTANT", "float"); }
{D}+{FS}                        { return emit(FLOATCONST, "CONSTANT", "float"); }

0[xX]{H}+[uU]                   { return emit(UINTCONST, "CONSTANT", "uint"); }
0{O}*[uU]                       { return emit(UINTCONST, "CONSTANT", "uint"); }
{NZ}{D}*[uU]                    { return emit(UINTCONST, "CONSTANT", "uint"); }
0[xX]{H}+                       { return emit(INTCONST, "CONSTANT", "int (hex)"); }
0{O}+                           { return emit(INTCONST, "CONSTANT", "int (octal)"); }
0                               { return emit(INTCONST, "CONSTANT", "int"); }
{NZ}{D}*                        { return emit(INTCONST, "CONSTANT", "int"); }

{D}+{L}({L}|{D})*               { lex_error("malformed number"); }
0{O}*[89]{D}*                   { lex_error("invalid octal constant"); }

{L}({L}|{D})*                   {
                                    struct keyword *kw = lookup_keyword(yytext);
                                    if (kw != 0) {
                                        if (kw->token == RESERVED) {
                                            lex_error("reserved keyword");
                                        } else if (kw->token == BOOLCONST) {
                                            return emit(BOOLCONST, "CONSTANT", "bool");
                                        } else {
                                            yylval.str = strdup(yytext);
                                            return emit(kw->token, "KEYWORD", kw->category);
                                        }
                                    } else if (is_struct_name(yytext)) {
                                        yylval.str = strdup(yytext);
                                        return emit(TYPE_NAME, "IDENTIFIER", "struct type");
                                    } else {
                                        yylval.str = strdup(yytext);
                                        if (strncmp(yytext, "gl_", 3) == 0)
                                            return emit(IDENTIFIER, "IDENTIFIER", "built-in");
                                        return emit(IDENTIFIER, "IDENTIFIER", 0);
                                    }
                                }

"<<="                           { return emit(SHL_ASSIGN, "OPERATOR", "assignment"); }
">>="                           { return emit(SHR_ASSIGN, "OPERATOR", "assignment"); }
"+="                            { return emit(ADD_ASSIGN, "OPERATOR", "assignment"); }
"-="                            { return emit(SUB_ASSIGN, "OPERATOR", "assignment"); }
"*="                            { return emit(MUL_ASSIGN, "OPERATOR", "assignment"); }
"/="                            { return emit(DIV_ASSIGN, "OPERATOR", "assignment"); }
"%="                            { return emit(MOD_ASSIGN, "OPERATOR", "assignment"); }
"&="                            { return emit(AND_ASSIGN, "OPERATOR", "assignment"); }
"|="                            { return emit(OR_ASSIGN, "OPERATOR", "assignment"); }
"^="                            { return emit(XOR_ASSIGN, "OPERATOR", "assignment"); }
"++"                            { return emit(INC, "OPERATOR", "inc/dec"); }
"--"                            { return emit(DEC, "OPERATOR", "inc/dec"); }
"=="                            { return emit(EQ, "OPERATOR", "relational"); }
"!="                            { return emit(NE, "OPERATOR", "relational"); }
"<="                            { return emit(LE, "OPERATOR", "relational"); }
">="                            { return emit(GE, "OPERATOR", "relational"); }
"<"|">"                         { return emit(yytext[0], "OPERATOR", "relational"); }
"&&"                            { return emit(AND, "OPERATOR", "logical"); }
"||"                            { return emit(OR, "OPERATOR", "logical"); }
"^^"                            { return emit(XOR, "OPERATOR", "logical"); }
"!"                             { return emit('!', "OPERATOR", "logical"); }
"<<"                            { return emit(SHL, "OPERATOR", "bitwise"); }
">>"                            { return emit(SHR, "OPERATOR", "bitwise"); }
"&"|"|"|"^"|"~"                 { return emit(yytext[0], "OPERATOR", "bitwise"); }
"+"|"-"|"*"|"/"|"%"             { return emit(yytext[0], "OPERATOR", "arithmetic"); }
"="                             { return emit('=', "OPERATOR", "assignment"); }
"?"|":"                         { return emit(yytext[0], "OPERATOR", "conditional"); }
"."                             { return emit('.', "OPERATOR", "member"); }

"("|")"|"["|"]"|"{"|"}"|";"|"," { return emit(yytext[0], "PUNCTUATOR", 0); }

.                               { lex_error("unexpected character"); }

%%

void skip_block_comment()
{
    int c, prev = 0, start = lineno;
    for (;;) {
        c = input();
        if (c == 0 || c == EOF) {
            fprintf(stderr, "line %d: lexical error: unterminated comment\n", start);
            lex_errors++;
            return;
        }
        if (c == '\n')
            lineno++;
        if (prev == '*' && c == '/')
            return;
        prev = c;
    }
}

int yywrap()
{
    strcpy(last_lexeme, "end of file");
    return 1;
}
