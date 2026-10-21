/*
 * optimizer.h -- Phase 5: conservative optimization of the existing Quad IR.
 */
#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "icg.h"

extern Quad *optimized_quads;
extern int n_optimized_quads;

extern int optimization_constant_propagations;
extern int optimization_constant_folds;
extern int optimization_cse_eliminated;
extern int optimization_strength_reductions;
extern int optimization_dead_instructions;

void optimize_ir(int enabled);
void optimizer_print_tac(void);
void optimizer_print_quads(void);
void optimizer_print_stats(void);
void optimizer_free(void);

#endif
