# SNOBOL4 in CREXX

A SNOBOL4 interpreter written in [CREXX](https://github.com/adesutherland/CREXX)
(Level B). It implements the SNOBOL4 core: statements with subject / pattern /
replacement / goto fields, the full pattern repertoire with backtracking,
user-defined functions, tables, arrays, programmer-defined data types,
indirection, `EVAL`, `CODE`, and line-oriented `INPUT`/`OUTPUT`.

```
./snobol examples/hello.sno
./snobol -f program.sno        # fold identifiers to upper case (like CSNOBOL4 -f)
echo "1+2*3" | ./snobol examples/calc.sno
```

The launcher compiles `snobol.crexx` to bytecode on first use (about half a
minute) and afterwards starts in well under a second. Set `CREXX` to the
`crexx` driver if it is not on your `PATH`.

Data lines placed after the `END` statement are read by `INPUT` before stdin.

The manual is in [`docs/SNOBOL-CREXX-Guide.pdf`](docs/SNOBOL-CREXX-Guide.pdf) (Word version: [`docs/SNOBOL-CREXX-Guide.docx`](docs/SNOBOL-CREXX-Guide.docx)).

## What is supported

* **Statements**: labels, `;` separators, `+`/`.` continuation lines, `*` comments,
  `S(...)`, `F(...)`, unconditional and computed gotos (`:($X)`, including `CODE` values),
  `RETURN`/`FRETURN`/`NRETURN`, `END`.
* **Patterns**: `ANY NOTANY SPAN BREAK BREAKX LEN POS RPOS TAB RTAB REM ARB ARBNO
  BAL FAIL ABORT FENCE SUCCEED`, alternation, concatenation, conditional (`.`) and
  immediate (`$`) assignment, cursor (`@`), unevaluated/recursive patterns (`*X`),
  `&ANCHOR`.
* **Data**: strings, integers, reals, patterns, `ARRAY` (multi-dimensional, with
  bounds), `TABLE`, `DATA` types, `NAME` values (`.X`), `CODE`, expressions.
* **Functions**: `DEFINE` with locals and recursion, `APPLY`, `ARG`, `LOCAL`,
  `OPSYN`, `EVAL`, `CODE`, `CONVERT`, `COPY`, `SORT`/`RSORT`, `DATATYPE`,
  `REPLACE`, `SUBSTR`, `DUPL`, `LPAD`/`RPAD`, `TRIM`, `REVERSE`, `SIZE`,
  `IDENT`/`DIFFER`, `EQ NE LT LE GT GE`, `LEQ ... LGE`, `REMDR`, math
  (`SIN COS TAN ATAN EXP LN SQRT CHOP`), `INPUT`/`OUTPUT` association with
  units and files, `ENDFILE`, `REWIND`, `DUMP`, `EXIT`, `RANDOM`.
* **Keywords**: `&ANCHOR &ERRLIMIT &ERRTYPE &STCOUNT &STLIMIT &STNO &FNCLEVEL
  &LASTNO &ALPHABET &LCASE &UCASE &MAXLNGTH &DIGITS`.

Errors are fatal and reported on stderr with the statement number, unless
`&ERRLIMIT` is positive, in which case the statement simply fails.

## Not implemented

`LOAD`/`UNLOAD`, `TRACE`/`STOPTR` (accepted, ignored), `&TRACE`, `HOST`,
`COLLECT` (no-op), user redefinition of operators through `OPSYN`.

## Layout

| Path | Purpose |
|------|---------|
| `src/*.crexx` | interpreter sources (parser, values, matcher, evaluator, runner) |
| `build.py` | assembles `snobol.crexx` from `src/` (substitutes symbolic constants) |
| `snobol.crexx` | the generated, self-contained interpreter |
| `snobol` | launcher script |
| `examples/` | sample programs |
| `tests/` | regression tests: `tests/run_tests.sh` |
| `docs/` | the manual (Word and PDF) |

Rebuild after editing `src/`: `python3 build.py`.
