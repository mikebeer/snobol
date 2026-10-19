#!/bin/sh
# Run the shared regression tests (../tests/NAME.sno, expected NAME.out) with
# the C interpreter.  Tests listed in tests/skip.txt are not run.
# usage: sh tests/run_tests.sh [interpreter]     (default ../snobol)
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$DIR/../.."
SNO=${1:-"$DIR/../snobol"}
pass=0; fail=0; skip=0
for src in "$ROOT"/tests/*.sno; do
  name=$(basename "$src" .sno)
  if grep -qx "$name" "$DIR/skip.txt" 2>/dev/null; then
    skip=$((skip+1)); echo "skip $name"; continue
  fi
  input=/dev/null; [ -f "$ROOT/tests/$name.in" ] && input="$ROOT/tests/$name.in"
  flags=""; [ -f "$ROOT/tests/$name.flags" ] && flags=$(cat "$ROOT/tests/$name.flags")
  actual=$("$SNO" $flags "$src" < "$input" 2>&1)
  if [ "$actual" = "$(cat "$ROOT/tests/$name.out")" ]; then
    pass=$((pass+1)); echo "ok   $name"
  else
    fail=$((fail+1)); echo "FAIL $name"
    if [ -n "$VERBOSE" ]; then
      printf '%s\n' "$actual" > "${TMPDIR:-/tmp}/snobol_actual.txt"
      diff "${TMPDIR:-/tmp}/snobol_actual.txt" "$ROOT/tests/$name.out" | head -"$VERBOSE"
    fi
  fi
done
echo "passed $pass, failed $fail, skipped $skip"
[ "$fail" -eq 0 ]
