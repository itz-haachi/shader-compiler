#!/bin/sh
# Runs every test shader and checks the compiler's verdict.
#   tests/valid/*            must compile (exit status 0)
#   tests/semantic_errors/*  must be rejected (exit status 1)
#   tests/syntax_errors/*    must be rejected (exit status 1)
# Usage: sh run_tests.sh          (build ./minic first)

pass=0
fail=0
tmp="${TMPDIR:-/tmp}/minic_phase5_$$.out"
trap 'rm -f "$tmp"' EXIT

check() {   # check <file> <expected exit status>
    ./minic --ir-only "$1" > /dev/null 2>&1
    status=$?
    if [ "$status" -eq "$2" ]; then
        echo "PASS  $1"
        pass=$((pass + 1))
    else
        echo "FAIL  $1  (exit $status, expected $2)"
        fail=$((fail + 1))
    fi
}

check_contains() {   # check_contains <file> <compiler args> <literal> <label>
    file=$1
    shift
    literal=$1
    shift
    label=$1
    shift
    ./minic "$@" "$file" > "$tmp" 2>&1
    status=$?
    if [ "$status" -eq 0 ] && grep -Fq "$literal" "$tmp"; then
        echo "PASS  $label"
        pass=$((pass + 1))
    else
        echo "FAIL  $label"
        fail=$((fail + 1))
    fi
}

check_not_contains() {   # check_not_contains <file> <compiler args> <literal> <label>
    file=$1
    shift
    literal=$1
    shift
    label=$1
    shift
    ./minic "$@" "$file" > "$tmp" 2>&1
    status=$?
    if [ "$status" -eq 0 ] && ! grep -Fq "$literal" "$tmp"; then
        echo "PASS  $label"
        pass=$((pass + 1))
    else
        echo "FAIL  $label"
        fail=$((fail + 1))
    fi
}

check test.frag 0
for f in tests/valid/*;           do check "$f" 0; done
for f in tests/semantic_errors/*; do check "$f" 1; done
for f in tests/syntax_errors/*;   do check "$f" 1; done

for f in tests/optimizer_codegen/*.frag; do check "$f" 0; done

check_contains tests/optimizer_codegen/constant_folding.frag "a = 5.0" "constant folding"
check_contains tests/optimizer_codegen/constant_propagation.frag "b = 5.0" "constant propagation"
check_contains tests/optimizer_codegen/constant_types.frag "value = 3u" "typed integer constant folding"
check_contains tests/optimizer_codegen/constant_types.frag "flag = false" "boolean constant folding"
check_contains tests/optimizer_codegen/hex_constant.frag "result = 255" "hexadecimal integer propagation"
check_contains tests/optimizer_codegen/double_precision.frag "result = 1.23456789012345" "double constant precision"
check_contains tests/optimizer_codegen/reassignment.frag "b = a + 2.0" "reassignment invalidation"
check_not_contains tests/optimizer_codegen/reassignment.frag "b = 7.0" "no stale reassignment constant"
check_contains tests/optimizer_codegen/strength_reduction.frag "Strength reductions           : 2" "strength reduction statistics"
check_contains tests/optimizer_codegen/strength_reduction.frag "doubled = inputValue + inputValue" "multiplication strength reduction"
check_contains tests/optimizer_codegen/cse.frag "second = first" "common subexpression elimination"
check_contains tests/optimizer_codegen/cse_self_update.frag "ADD result, a, rightValue" "CSE preserves self-updating operands"
check_contains tests/optimizer_codegen/uint_not.frag "result = 4294967295u" "32-bit unsigned unary folding"
check_contains tests/optimizer_codegen/dot_scope.frag "DP4 result, v, w" "function-scoped dot specialization"
check_not_contains tests/optimizer_codegen/dead_temp.frag "ADD" "dead temporary elimination" --target-only
check_contains tests/optimizer_codegen/vector_backend.frag "ADD sum, leftVector, rightVector" "vector ADD target instruction"
check_contains tests/optimizer_codegen/constructors_calls.frag "VEC4 R0, value, 1.0" "constructor target instruction"
check_contains tests/optimizer_codegen/function_call.frag "CALL twice" "function CALL target instruction"
check_contains tests/optimizer_codegen/dot_texture.frag "NORM normal, leftVector" "normalize target instruction"
check_contains tests/optimizer_codegen/dot_texture.frag "TEX sampleColor, textureSampler, coordinates" "texture target instruction"
check_contains tests/optimizer_codegen/dot_texture.frag "DP3 amount, leftVector, rightVector" "typed dot target instruction"
check_contains tests/optimizer_codegen/dot_vec4.frag "DP4 result, leftVector, rightVector" "typed vec4 dot target instruction"
check_contains tests/optimizer_codegen/dot_generic.frag "CALL dot" "generic dot target fallback"
check_contains tests/optimizer_codegen/conversion.frag "CONV result, inputValue, float" "conversion target instruction"
check_contains tests/optimizer_codegen/array_indexing.frag "LOADIDX result, weights, indexValue" "array load target instruction"
check_contains tests/optimizer_codegen/length_builtin.frag "LEN result, inputVector" "vector length target instruction"
check_contains tests/optimizer_codegen/array_length.frag "ARRAYLEN result, values" "runtime array length target instruction"
check_contains tests/optimizer_codegen/discard.frag "DISCARD" "discard target instruction"
check_contains tests/optimizer_codegen/swizzle_write.frag "MOV color.xyz, inputColor" "swizzle write target instruction"
check_contains tests/optimizer_codegen/nested_store.frag "MOV R0.xy, inputValue" "temporary nested swizzle destination mapping"
check_contains tests/optimizer_codegen/if_else.frag "CMP_GT R0, conditionValue, 0.0" "conditional comparison target instruction"
check_contains tests/optimizer_codegen/while_loop.frag "LABEL L1" "while-loop target labels"
check_contains tests/optimizer_codegen/for_loop.frag "JMP L1" "for-loop target jump"
check_contains tests/optimizer_codegen/division_by_zero.frag "DIV value, 5, 0" "division by zero remains unfurled"
check_contains tests/optimizer_codegen/unsafe_float_strength.frag "Strength reductions           : 0" "unsafe float strength reduction rejected"
check_contains tests/optimizer_codegen/signed_shift.frag "SHL result, inputValue, 1u" "signed shift strength reduction rejected"
check_contains tests/optimizer_codegen/constant_folding.frag "ADD a, 2.0, 3.0" "--no-opt preserves original arithmetic" --no-opt --target-only
check_not_contains tests/optimizer_codegen/constant_folding.frag "TARGET CODE" "--no-target suppresses assembly" --no-target
check_not_contains tests/optimizer_codegen/constant_folding.frag "TARGET CODE" "--ir-only suppresses assembly" --ir-only
check_not_contains tests/optimizer_codegen/constant_folding.frag "SYNTAX ANALYSIS" "--target-only suppresses earlier phase output" --target-only

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
