#include <gtest/gtest.h>

extern "C" {
extern int lex_errors;
#include "y.tab.h"                 // token numbers like TYPE_NAME, IDENTIFIER
extern int lineno;
int yylex(void);                   // the lexer: returns the next token
extern int show_tokens;            // your lexer prints tokens when this is 1
typedef struct yy_buffer_state *YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_string(const char *str);
void yy_delete_buffer(YY_BUFFER_STATE buffer);
}
class LexerTest : public ::testing::Test {
protected:
	void SetUp() override {
		show_tokens = 0;
		lineno = 1;
		lex_errors = 0;
	}
};
TEST_F(LexerTest, Vec3IsTypeName) {
    YY_BUFFER_STATE buf = yy_scan_string("vec3");    // Arrange: feed a string
    EXPECT_EQ(yylex(), TYPE_NAME);                   // Act + Assert
    yy_delete_buffer(buf);                           // clean up
}
TEST_F(LexerTest,DeclarationTokenSequence) {
	YY_BUFFER_STATE buf = yy_scan_string("vec3 a = b;");
	EXPECT_EQ(yylex(), TYPE_NAME); 
	EXPECT_EQ(yylex(),IDENTIFIER);
	EXPECT_EQ(yylex(),'=');
	EXPECT_EQ(yylex(),IDENTIFIER);
	EXPECT_EQ(yylex(),';');
	EXPECT_EQ(yylex(),0);
	yy_delete_buffer(buf);
}
TEST_F(LexerTest,NewlineIncrementsLineNumber) {
	YY_BUFFER_STATE buf = yy_scan_string("vec3\na");
        EXPECT_EQ(yylex(), TYPE_NAME);
        EXPECT_EQ(yylex(),IDENTIFIER);
	yy_delete_buffer(buf);
}
TEST_F(LexerTest,IfIsKeyword) {
	YY_BUFFER_STATE buf = yy_scan_string("if");
	EXPECT_EQ(yylex(),IF);
        yy_delete_buffer(buf);
}
TEST_F(LexerTest,FloatConstant) {
        YY_BUFFER_STATE buf = yy_scan_string("3.14");
	EXPECT_EQ(yylex(),FLOATCONST);
        yy_delete_buffer(buf);
}
TEST_F(LexerTest,IntConstant) {
        YY_BUFFER_STATE buf = yy_scan_string("42");
        EXPECT_EQ(yylex(),INTCONST);
        yy_delete_buffer(buf);
}
TEST_F(LexerTest,LineCommentIsSkipped) {
	YY_BUFFER_STATE buf = yy_scan_string("// hello\nx");
	EXPECT_EQ(yylex(),IDENTIFIER);
	EXPECT_EQ(yylex(),0);
	EXPECT_EQ(lineno,2);
        yy_delete_buffer(buf);
}
TEST_F(LexerTest,BlockedCommentIsSkipped) {
        YY_BUFFER_STATE buf = yy_scan_string("/* a\nb */ x");
        EXPECT_EQ(yylex(),IDENTIFIER);
        EXPECT_EQ(yylex(),0);
        EXPECT_EQ(lineno,2);
        yy_delete_buffer(buf);
}
TEST_F(LexerTest,UnterminatedBlockCommentIsError) {
        YY_BUFFER_STATE buf = yy_scan_string("/* never closed");
	EXPECT_EQ(yylex(),0);
	EXPECT_EQ(lex_errors,1);
        yy_delete_buffer(buf);
}
