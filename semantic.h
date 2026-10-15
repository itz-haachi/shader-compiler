/*
 * semantic.h -- Phase 3: semantic analysis.
 */
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

extern int semantic_errors;
extern int semantic_warnings;

void semantic_analyze(Node *program);

#endif
