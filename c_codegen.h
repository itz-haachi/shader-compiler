/*
 * c_codegen.h -- Phase 7: standalone C source generation.
 *
 * The generated program contains a small MGValue runtime.  It is deliberately
 * self-contained, so the result can be compiled with a normal C99 compiler
 * without a GLSL SDK.
 */
#ifndef C_CODEGEN_H
#define C_CODEGEN_H

void c_codegen_generate(void);
void c_codegen_print(void);
int  c_codegen_write_file(const char *path);
void c_codegen_free(void);

#endif
