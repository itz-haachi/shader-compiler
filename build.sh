#!/bin/sh
# Build MiniGLSL without make:  sh build.sh
set -e
bison -y -d shader.y          # or: yacc -d shader.y   -> y.tab.c, y.tab.h
flex shader_final.l           # or: lex shader_final.l -> lex.yy.c
gcc -w -o minic lex.yy.c y.tab.c ast.c types.c symtab.c semantic.c icg.c optimizer.c codegen.c c_codegen.c main.c
echo "built ./minic"
