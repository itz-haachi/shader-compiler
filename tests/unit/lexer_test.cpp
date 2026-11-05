#include <gtest/gtest.h>

extern "C" {
#include "y.tab.h"                 // token numbers like TYPE_NAME, IDENTIFIER
int yylex(void);                   // the lexer: returns the next token
extern int show_tokens;            // your lexer prints tokens when this is 1
typedef struct yy_buffer_state *YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_string(const char *str);
void yy_delete_buffer(YY_BUFFER_STATE buffer);
}

TEST(LexerTest, Vec3IsTypeName) {
    show_tokens = 0;                                 // keep output quiet
    YY_BUFFER_STATE buf = yy_scan_string("vec3");    // Arrange: feed a string
    EXPECT_EQ(yylex(), TYPE_NAME);                   // Act + Assert
    yy_delete_buffer(buf);                           // clean up
}
TEST(LexerTest,DeclarationTokenSequence) {
	show_tokens = 0;
	YY_BUFFER_STATE buf = yy_scan_string("vec3 a = b;");
	EXPECT_EQ(yylex(), TYPE_NAME); 
	EXPECT_EQ(yylex(),IDENTIFIER);
	EXPECT_EQ(yylex(),'=');
	EXPECT_EQ(yylex(),IDENTIFIER);
	EXPECT_EQ(yylex(),';');
	EXPECT_EQ(yylex(),0);
	yy_delete_buffer(buf);
}
