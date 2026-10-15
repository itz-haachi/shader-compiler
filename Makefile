# MiniGLSL -- build with:  make
# Needs lex/flex, yacc/bison and gcc (Ubuntu: sudo apt install flex bison gcc make)

CC     = gcc
LEX    = flex
YACC   = bison -y
CFLAGS = -w

OBJ_SRC = lex.yy.c y.tab.c ast.c types.c symtab.c semantic.c icg.c main.c
HEADERS = ast.h types.h symtab.h semantic.h icg.h

minic: $(OBJ_SRC) $(HEADERS)
	$(CC) $(CFLAGS) -o minic $(OBJ_SRC)

y.tab.c y.tab.h: shader.y ast.h
	$(YACC) -d shader.y

lex.yy.c: shader_final.l y.tab.h
	$(LEX) shader_final.l

test: minic
	sh run_tests.sh

clean:
	rm -f minic minic.exe lex.yy.c y.tab.c y.tab.h y.output

.PHONY: test clean
