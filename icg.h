/*
 * icg.h -- Phase 4: intermediate code generation.
 *
 * Three-address code stored as quadruples (op, arg1, arg2, result).
 * Temporaries (t1, t2, ...) carry a GLSL type, so a temporary can hold
 * a whole vector or matrix (vector-valued temporaries).
 */
#ifndef ICG_H
#define ICG_H

#include "ast.h"

typedef struct {
    char *op;
    char *arg1;
    char *arg2;
    char *result;
} Quad;

extern Quad *quads;
extern int n_quads;

void icg_generate(Node *program);
void icg_print_tac(void);
void icg_print_quads(void);
void icg_print_temps(void);

#endif
