#!/bin/sh
# Benchmarks: run each bench/NAME.sno with the C interpreter (and with the
# CREXX one when the crexx driver is available), compare with NAME.out and
# print the times.   usage: sh bench/run.sh            (from the repo root)
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$DIR/.."
CSNO="$ROOT/c/snobol"
[ -x "$CSNO" ] || (cd "$ROOT/c" && make >/dev/null) || exit 1
have_crexx=0
if [ -x "$ROOT/snobol" ] && { [ -n "$CREXX" ] || command -v crexx >/dev/null 2>&1; }; then have_crexx=1; fi
bad=0
for src in "$DIR"/*.sno; do
  name=$(basename "$src" .sno)
  s=$(date +%s)
  out=$("$CSNO" "$src" 2>&1)
  e=$(date +%s)
  if [ "$out" = "$(cat "$DIR/$name.out")" ]; then r=ok; else r=FAIL; bad=1; fi
  line="$name: C $r $((e-s))s"
  if [ $have_crexx = 1 ]; then
    s=$(date +%s)
    out=$("$ROOT/snobol" "$src" 2>&1)
    e=$(date +%s)
    if [ "$out" = "$(cat "$DIR/$name.out")" ]; then r=ok; else r=FAIL; bad=1; fi
    line="$line, CREXX $r $((e-s))s"
  fi
  echo "$line"
done
exit $bad
