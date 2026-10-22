# MiniGLSL Optimization and Target Code Generation Design

## Goal

Extend the existing MiniGLSL compiler with Phase 5 optimization and Phase 6 educational vector-machine target code generation while preserving the existing lexer, parser, semantic analyzer, AST, symbol table, and Quad-based Phase 4 IR.

## Scope and constraints

- Work continues in the existing repository; no second compiler architecture is introduced.
- The optimizer consumes the existing `Quad *quads` / `n_quads` representation.
- Original quads remain available for before/after display.
- Target code is generated only from `optimized_quads`.
- Earlier compiler behavior and existing CLI options remain compatible.
- No external compiler framework or hardware-specific assembly dependency is added.
- The target is explicitly readable educational vector assembly, not PTX, SPIR-V, or real hardware machine code.

## Phase 5 architecture

`optimizer.c` and `optimizer.h` own a deep-copied optimized Quad array and actual pass statistics. The public state exposes `optimized_quads` and `n_optimized_quads`, plus printing and cleanup functions.

The passes run in this order:

1. Constant propagation over straight-line regions. Scalar integer, unsigned integer, float, and boolean literals are tracked. Reassignment invalidates the old value.
2. Constant folding for supported arithmetic, comparisons, logical, unary, and integer bitwise operators. Division and modulo by zero are left unchanged. Vector and aggregate values are never treated as scalar constants.
3. Common-subexpression elimination within safe basic-block regions. Binary and unary pure expressions are reused only while their operands remain unmodified.
4. Strength reduction for provably safe scalar expressions. The implementation applies the modulo-equivalent multiplication/addition and shift rewrites to unsigned integer operands (for example `x * 2u` → `x + x`, `x / 2u` → `x >> 1u`, and `x << 1u` → `x * 2u`). Signed overflow/shift corner cases and floating-point rounding/signed-zero behavior are left unchanged.
5. Conservative dead-code elimination. Only unused temporary-producing, side-effect-free calculations are removed. Named-variable writes, calls, parameter passing, control flow, stores, texture/unknown calls, discard, return, and shader outputs are retained.

Control-flow boundaries (`label`, `goto`, `if`, `ifFalse`, `func`, `endfunc`, `call`, `refparam`, `return`, and `discard`) clear propagation and CSE state. Aggregate stores (`swizzle=`, `member=`, `[]=`) also clear state to avoid aliasing mistakes. Passes are bounded and do not loop indefinitely.

Statistics count actual transformations: constant propagations, constant folds, common subexpressions removed, strength reductions, and dead instructions removed.

## Phase 6 architecture

`codegen.c` and `codegen.h` translate optimized quads deterministically. TAC temporaries map by numeric suffix (`t1` to `R0`, `t2` to `R1`, and so on). Arithmetic and logical operators map to readable vector instructions; vector values remain vector operands and are not scalarized.

The backend buffers adjacent `param` / `refparam` values so constructors and calls can be emitted as a single readable instruction. Constructors support `VEC2`, `VEC3`, `VEC4`, `MAT2`, `MAT3`, and `MAT4`, with generic construction fallback. Built-in calls specialize safely by function name: `texture` to `TEX`, `normalize` to `NORM`, and vector `length` to `LEN`; the IR `length` operation for runtime array length remains distinct. Dot products become `DP3` or `DP4` only when operand type metadata proves the width, otherwise they use generic `CALL`.

Control flow maps to `LABEL`, `JMP`, conditional jump pseudo-instructions, comparison instructions, and function/call/return instructions. Swizzles and members remain readable dotted operands. Array access uses `LOADIDX` and `STOREIDX`. `discard` always emits `DISCARD`.

## Driver and CLI

`main.c` runs optimization after successful ICG and target generation after successful optimization. `--no-opt` preserves a copy of the original IR without transformations, `--no-target` suppresses target output, `--target-only` suppresses verbose intermediate displays while still validating all earlier phases, and `--ir-only` remains IR-focused and does not print target assembly.

## Testing

New shader fixtures cover constant folding, propagation, reassignment invalidation, CSE, dead temporary removal, strength reduction, if/else and loops, discard, vector operations, swizzles, constructors, calls, dot/texture built-ins, conversion, and indexing. Shell-level assertions inspect optimized TAC and vector assembly. Existing valid and invalid shader tests remain part of `make test`.

## Expected output

The driver displays original TAC, optimization counts and optimized TAC/quadruples, followed by educational vector assembly. For example, `a = 2.0 + 3.0; b = a * 4.0;` may become `a = 5.0; b = 20.0;` and emit `MOV a, 5.0` / `MOV b, 20.0` inside the function body.
