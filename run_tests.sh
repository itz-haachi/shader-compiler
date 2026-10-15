#!/bin/sh
# Runs every test shader and checks the compiler's verdict.
#   tests/valid/*            must compile (exit status 0)
#   tests/semantic_errors/*  must be rejected (exit status 1)
#   tests/syntax_errors/*    must be rejected (exit status 1)
# Usage: sh run_tests.sh          (build ./minic first)

pass=0
fail=0

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

check test.frag 0
for f in tests/valid/*;           do check "$f" 0; done
for f in tests/semantic_errors/*; do check "$f" 1; done
for f in tests/syntax_errors/*;   do check "$f" 1; done

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
