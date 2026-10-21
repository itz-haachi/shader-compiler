# MiniGLSL Optimization and Target Code Generation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add conservative Quad-based optimization, strength reduction, and deterministic educational vector assembly to the existing MiniGLSL compiler.

**Architecture:** Preserve Phase 1–4 unchanged as the producer of `quads`. Add an optimizer that owns a deep-copied `optimized_quads` array and statistics, then add a backend that consumes only that optimized array. Integrate both behind existing phase gates and compatible CLI switches.

**Tech Stack:** C, Flex, Bison/Yacc, GCC, Make, POSIX shell test scripts.

**Spec:** `docs/superpowers/specs/2026-10-21-optimization-target-codegen-design.md`

## Global Constraints

- The optimizer consumes the existing `Quad *quads` / `n_quads` representation.
- Original quads remain available for before/after display.
- Target code is generated only from `optimized_quads`.
- Earlier compiler behavior and existing CLI options remain compatible.
- The target is readable educational vector assembly, not hardware assembly.
- Control-flow and side-effect boundaries are optimized conservatively.
- No external compiler framework or second incompatible IR is introduced.

## Review Focus

- Reassignment and branch boundaries must not leak stale constants; covered by propagation and if/loop fixtures.
- Vector/aggregate operands must not be scalar-folded or DCE'd; covered by vector, swizzle, and constructor fixtures.
- Calls, parameter passing, discard, output stores, and indexing must remain observable; covered by call/texture/discard/index fixtures.
- Strength reduction must not alter floating-point, signed-overflow, or divide-by-zero behavior; covered by unsigned-safe and unsafe arithmetic fixtures.
- Temporary renumbering and constructor parameter collection must remain deterministic; covered by assembly pattern checks.

---

### Task 1: Toolchain baseline and optimizer test harness

**Files:**
- Modify: `run_tests.sh`
- Create: `tests/optimizer_codegen/constant_folding.frag`
- Create: `tests/optimizer_codegen/constant_propagation.frag`
- Create: `tests/optimizer_codegen/reassignment.frag`
- Create: `tests/optimizer_codegen/strength_reduction.frag`

**Interfaces:**
- Consumes the existing compiler executable and its current `--ir-only` behavior.
- Produces focused shader fixtures and shell assertions that later optimizer code must satisfy.

- [x] Install or select the minimal available Flex/Bison/Make toolchain without changing compiler dependencies, then run the unmodified baseline build/test and record any pre-existing failures.
- [x] Add the first fixtures and assertions for folding, propagation, reassignment invalidation, and strength reduction; assert failure before the new optimizer exists.
- [x] Keep existing valid/error exit-status checks unchanged.

### Task 2: Optimizer module — propagation, folding, CSE, strength reduction, DCE

**Files:**
- Create: `optimizer.h`
- Create: `optimizer.c`
- Create: `tests/optimizer_codegen/cse.frag`
- Create: `tests/optimizer_codegen/dead_temp.frag`
- Create: `tests/optimizer_codegen/unsafe_control_flow.frag`

**Interfaces:**
- Consumes: `quads`, `n_quads` from `icg.h`.
- Produces: `optimized_quads`, `n_optimized_quads`, `optimize_ir(int enabled)`, `optimizer_print_tac()`, `optimizer_print_quads()`, `optimizer_print_stats()`, and `optimizer_free()`.

- [x] Add failing shell assertions for CSE reuse, removal of unused temporary calculations, and no stale propagation across a conditional assignment.
- [x] Implement deep-copy/reset/free helpers and literal parsing that distinguishes int, uint, float, and bool scalar constants.
- [x] Implement conservative propagation and folding, including comparisons, logical/unary operators, integer bitwise operators, and divide/modulo-by-zero guards.
- [x] Implement basic-block CSE with operand invalidation and actual statistics.
- [x] Implement safe unsigned-integer strength reductions and explicit rejection of unsafe signed/floating-point and zero-divisor rewrites; count reductions separately.
- [x] Implement iterative global-use-count DCE restricted to pure temporary-producing calculations.
- [x] Run the focused fixtures and existing suite; refactor only after green.

### Task 3: Vector-machine target backend

**Files:**
- Create: `codegen.h`
- Create: `codegen.c`
- Create: `tests/optimizer_codegen/vector_backend.frag`
- Create: `tests/optimizer_codegen/constructors_calls.frag`
- Create: `tests/optimizer_codegen/control_flow.frag`

**Interfaces:**
- Consumes: `optimized_quads`, `n_optimized_quads` from `optimizer.h` and existing Quad metadata.
- Produces: `codegen_generate()` and `codegen_print()` for deterministic vector assembly output.

- [x] Add failing output assertions for arithmetic, comparisons/jumps, function boundaries, vector operations, swizzles, indexing, discard, conversion, constructors, user calls, texture, normalize, and generic dot fallback.
- [x] Implement deterministic `tN` to `RN` mapping and readable operand formatting.
- [x] Implement pending `param` / `refparam` collection for constructors and calls, with `VEC*` / `MAT*`, `PUSH`, `CALL`, and `RETVAL` conventions.
- [x] Implement operator, comparison, control-flow, function, swizzle/member, indexing, conversion, discard, and built-in mappings while keeping IR `length` distinct from vector `LEN`.
- [x] Run backend fixtures and inspect assembly output for semantic consistency.

### Task 4: Driver, build, and complete fixture coverage

**Files:**
- Modify: `main.c`
- Modify: `Makefile`
- Modify: `run_tests.sh`
- Create: `tests/optimizer_codegen/if_else.frag`
- Create: `tests/optimizer_codegen/while_loop.frag`
- Create: `tests/optimizer_codegen/for_loop.frag`
- Create: `tests/optimizer_codegen/discard.frag`
- Create: `tests/optimizer_codegen/swizzle_read.frag`
- Create: `tests/optimizer_codegen/swizzle_write.frag`
- Create: `tests/optimizer_codegen/constructor.frag`
- Create: `tests/optimizer_codegen/function_call.frag`
- Create: `tests/optimizer_codegen/dot_texture.frag`
- Create: `tests/optimizer_codegen/conversion.frag`
- Create: `tests/optimizer_codegen/array_indexing.frag`

**Interfaces:**
- Consumes: optimizer and codegen public interfaces from Tasks 2–3.
- Produces: integrated Phase 5/6 execution, CLI switches, build targets, and regression coverage.

- [x] Add failing integration checks for `--no-opt`, `--no-target`, `--target-only`, and `--ir-only` semantics.
- [x] Integrate Phase 5 after successful ICG and Phase 6 after successful optimization; never generate target code for invalid shaders.
- [x] Update Makefile source/header lists and preserve `make`, `make test`, and `make clean`.
- [x] Add all remaining requested valid shaders and run each individually.
- [x] Update README phase/usage documentation only where needed to describe the new output and options.

### Task 5: Full verification and final review

**Files:**
- Modify: any implementation/test files required by verification findings.

- [x] Run `make clean`, `make`, and `make test` from the repository root.
- [x] Run `./minic test.frag` and every new optimization/codegen fixture individually.
- [x] Inspect original TAC, optimized TAC, optimization statistics, and vector assembly for representative shaders.
- [x] Run a final self-review against the spec, preserving all earlier-phase behavior and recording any deferred minor concerns.

