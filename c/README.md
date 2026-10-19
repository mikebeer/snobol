# SNOBOL4 in portable C89

A C89 version of the SNOBOL4 interpreter, written to run later on VM/370
(CMS).  It is a direct port of the CREXX sources in `../src/*.crexx`: same
parser, same matcher, same evaluator, same error numbers and messages.  It
passes the same regression tests (`../tests/*.sno`) with identical output.
Layout, rules and checks follow `mikebeer/apl` and the Smalltalk port.

    make                  # builds ./snobol from the single file snobol_all.c
    make test             # runs ../tests against ./snobol
    make strict           # gcc/clang strict matrix, sanitizers, -m32, deck
    make deck             # the 71-column card deck deck/snobol_deck.c
    make bench            # ../bench programs (C, and CREXX if it is installed)
    ./snobol [-f] prog.sno

`-f` folds identifiers to upper case, as in the CREXX version.  Data lines
after the `END` statement are read by `INPUT` before standard input.

## Layout

    src/s0_base.c    types, reference counted strings, maps, number printing
    src/s1_val.c     value registers, errors, numbers, I/O lines, variables,
                     keywords, arrays, tables, DATA types
    src/s2_parse.c   program loading, statements, expression parser
    src/s3_pat.c     pattern nodes, backtracking matcher, statement execution
    src/s4_eval.c    arithmetic, lvalues, evaluator, calls, DEFINE, EVAL/CODE
    src/s5_bi.c      built-in functions, SORT, CONVERT, COPY, I/O association
    src/s6_main.c    predefined variables, main
    src/s_gen.h      constants and built-in numbers (tools/gen_consts.py,
                     from ../src/consts.txt and the function list)
    src/snobol.c     includes the parts; tools/amalg.sh inlines them into
    snobol_all.c     ONE self-contained source file (generated; checked in)
    deck/            the card-image build (generated; checked in)
    tools/           amalg.sh, gen_consts.py, deck.py, strict.sh
    tests/           run_tests.sh, skip.txt

## Portability rules

* Strict C89: no `//`, no declarations after statements, no `long long`, no
  `stdint.h`, no VLAs, no C99 library functions.  `gcc` and `clang` with
  `-std=c89 -pedantic-errors -Wall -Wextra -Wconversion -Wshadow` are clean.
* All functions are `static`; the only external symbol is `main`.
* Character classes use literal sets (`LOWER`, `UPPER`, `SPACES`), never code
  point ranges; only the digits use `'0'..'9'`.  Nothing depends on ASCII.
* `long` may be 32 bits (VM/370).  Integers are `long`; `-m32` builds pass all
  tests.  Overflow is detected explicitly (no signed overflow is ever done).
* Operating system services are only used with `-DSNO_POSIX` (`gettimeofday`
  for `TIME()`; without it `clock()` is used).  Nothing else is needed.
* Tables of nodes, variables, elements, choice points and so on are plain
  arrays that grow with `realloc`: never keep a pointer across a call that can
  allocate.  Strings are reference counted and may contain NUL bytes.
* `tools/deck.py` folds the amalgamated file to 71 columns without changing the
  token stream (`make deck`, verified with `--verify`; `make deck-test` runs the
  whole suite against the deck build).

## Numbers (identical to the CREXX version)

* Integer and real values keep their types; `1 / 2` is integer division
  truncating towards zero, `REMDR` has the sign of the dividend, `**` with a
  negative exponent gives 0 (1 for a base of 1, +-1 for -1).
* A real is printed like CREXX prints a `.float`: at most 15 significant
  digits, trailing zeros removed, plain notation for decimal exponents from -6
  to 17, otherwise `d.ddde+XX`; then SNOBOL adds the decimal point and an upper
  case `E`: `3.`, `0.3`, `1.E+20`, `1.5E-10`, `inf.`.  The algorithm
  (`real_text`/`fmtreal` in `s0_base.c`) is a port of the CREXX runtime's
  `extract_double_decimal`/`RexxDecimalFormat`, so even the last digit of
  `2 ** 0.5` or `1.0 / 3.0` agrees.
* Integer overflow: the CREXX VM raises an overflow signal that the interpreter
  does not handle, so the target keeps its old contents; the result of an
  overflowing `+`, `-` or `*` is the previous result of the same operation
  (0 at first).  The same is done here (`last_add` and friends in `s4_eval.c`),
  which gives CREXX's output for `9223372036854775807 + 1`, `2 ** 63` and so
  on.  `0 - x` on the most negative integer leaves `x` unchanged in both.
  With a 32-bit `long` the same rules apply at +-2^31.
* Strings that look like integers but need more than 18 digits are reals.

## Checked

* `make test`: 10 passed, 0 failed, 0 skipped (`tests/skip.txt` is empty).
* gcc and clang, `-std=c89 -pedantic-errors` with the warnings above;
  AddressSanitizer + UBSan clean on all tests; `-m32` (32-bit `long`) gives
  the same test output.
* Differential testing against the CREXX version on about 40 hand-written
  programs (tables, arrays, DATA, DEFINE, CODE/EVAL, I/O association, goto
  forms, pattern primitives, error handling) and 1500 random expressions
  (arithmetic, strings, comparisons, numeric builtins): output identical
  except for the cases listed below.  The four programs in `../bench` give
  identical output with both versions.

## Differences from the CREXX version

None of them is exercised by the shared tests.

* **Bytes, not code points.**  CREXX strings are UTF-8 and `SIZE`, `SUBSTR`
  and the pattern functions count code points.  Here a character is a byte.
  `&ALPHABET` is the 256 raw bytes (CREXX gives the UTF-8 form of code points
  128..255).  ASCII text behaves the same.
* **Conversion panics.**  CREXX stops with a PANIC where a number does not fit:
  an integer literal of 19 or more digits in the program text, a real that is
  integral but beyond the integer range given to `LEN`, `SUBSTR` and the like,
  a real string such as `1e999` or `5e-324`.  Here the literal becomes a real,
  the argument is error 1 ("Illegal data type") and the string converts with
  `strtod`.
* **Two CREXX bugs are not reproduced.**  The CREXX `ITEM(A, i)` on an array
  passes the global register `ri` as a parameter and so uses the wrong array
  once `ri` changes; `EVAL(E)` of an unevaluated expression (`*X`, or
  `CONVERT(s, 'EXPRESSION')`) with sub-expressions panics for the same reason.
  The C version evaluates both correctly.
* `TIME()` is milliseconds of elapsed time (POSIX) or CPU time (plain C).
* The last digits of reals depend on the host's `log10`, `pow`, `sin`...  On
  IEEE hosts they agree with CREXX.  The VM/370 build uses hexadecimal
  floating point where printing 15 digits may differ in the last place.
* Syntax errors and file-open failures are printed on standard output (like
  CREXX's `say`), run-time errors on standard error after flushing standard
  output, and `TERMINAL` goes to standard error.

## Speed

`make bench` runs the four programs in `../bench` (loops and arithmetic, string
building and pattern matching, table inserts and lookups, recursion through
DEFINE).  On the development machine the C version takes 0.02 to 0.12 seconds
for each; the CREXX version takes 2 to 7 seconds.
