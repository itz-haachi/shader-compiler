cmake_minimum_required(VERSION 3.14)
project(MiniGLSL C)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

BISON_TARGET(Parser shader.y ${CMAKE_CURRENT_BINARY_DIR}/y.tab.c
             DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/y.tab.h
             COMPILE_FLAGS -y)
FLEX_TARGET(Lexer shader_final.l ${CMAKE_CURRENT_BINARY_DIR}/lex.yy.c)
ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

add_library(minicore STATIC
    ${BISON_Parser_OUTPUTS} ${FLEX_Lexer_OUTPUTS}
    ast.c types.c symtab.c semantic.c icg.c optimizer.c codegen.c c_codegen.c)
target_include_directories(minicore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(minicore PRIVATE -w)

add_executable(minic main.c)
target_link_libraries(minic minicore)

# ---------- unit tests ----------
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(unit_tests tests/unit/lexer_test.cpp tests/unit/parser_test.cpp)
target_link_libraries(unit_tests minicore GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(unit_tests)
