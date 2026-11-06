#include <gtest/gtest.h>

extern "C" {
#include "ast.h"                      // for ast_root (the parsed tree)
int yyparse(void);
extern int lineno, lex_errors, syntax_errors, show_tokens;
extern int n_globals, n_structs, n_functions, n_struct_names;
typedef struct yy_buffer_state *YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_string(const char *str);
void yy_delete_buffer(YY_BUFFER_STATE buffer);
}

class ParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        show_tokens = 0;
        lineno = 1;
        lex_errors = 0;
        syntax_errors = 0;
        ast_root = 0;
        n_globals = n_structs = n_functions = n_struct_names = 0;
    }

    // helper: returns true if the source parses with no errors
    bool parses(const char *src) {
        YY_BUFFER_STATE buf = yy_scan_string(src);
        int result = yyparse();
        yy_delete_buffer(buf);
        return result == 0 && syntax_errors == 0 && lex_errors == 0;
    }
};

TEST_F(ParserTest, EmptyMainParses) {
    EXPECT_TRUE(parses("void main() { }"));
}
