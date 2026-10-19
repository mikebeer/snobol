# SNOBOL4 interpreter in portable C89
CC ?= cc
CFLAGS ?= -O2
STD = -std=c89 -pedantic -Wall -Wextra
POSIX = -DSNO_POSIX
PYTHON ?= python3
DECKCOLS ?= 71
SRC = src/snobol.c src/s0_base.c src/s1_val.c src/s2_parse.c src/s3_pat.c \
      src/s4_eval.c src/s5_bi.c src/s6_main.c src/s_gen.h

snobol: snobol_all.c
	$(CC) $(STD) $(CFLAGS) -o snobol snobol_all.c -lm

posix: snobol_all.c
	$(CC) $(STD) $(CFLAGS) $(POSIX) -o snobol snobol_all.c -lm

snobol_all.c: $(SRC) tools/amalg.sh
	sh tools/amalg.sh > snobol_all.c

src/s_gen.h: ../src/consts.txt ../src/a_head.crexx tools/gen_consts.py
	$(PYTHON) tools/gen_consts.py > src/s_gen.h

test: snobol
	sh tests/run_tests.sh ./snobol

# the card deck for the VM/370 build: every line at most $(DECKCOLS) columns
deck: deck/snobol_deck.c

deck/snobol_deck.c: snobol_all.c tools/deck.py
	mkdir -p deck
	$(PYTHON) tools/deck.py --limit $(DECKCOLS) snobol_all.c > deck/snobol_deck.c
	$(PYTHON) tools/deck.py --verify snobol_all.c deck/snobol_deck.c
	$(PYTHON) tools/deck.py --check --limit $(DECKCOLS) deck/snobol_deck.c

# build the deck with strict flags and run the whole suite against it
deck-test: deck
	$(CC) $(STD) -pedantic-errors $(CFLAGS) -o snobol_deck deck/snobol_deck.c -lm
	sh tests/run_tests.sh ./snobol_deck

# strict C89 matrix (gcc, clang, sanitizers, 32-bit long)
strict: snobol_all.c deck
	sh tools/strict.sh

bench: snobol
	sh ../bench/run.sh

clean:
	rm -f snobol snobol_deck snobol_san snobol_32

.PHONY: posix test deck deck-test strict bench clean
