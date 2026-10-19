CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=

# Includes are written from the src/ root ("automata/nfa.h"); the public API is in include/.
INCLUDES = -Iinclude -Isrc

# Every module lives in a group folder src/<group>/; src/main.c is the CLI only.
LIB_SRC = $(wildcard src/*/*.c)
LIB_HDR = $(wildcard include/*.h src/*/*.h)

# The oracle (tests/oracle.c) needs POSIX <regex.h>, which strict -std=c11 hides.
ORACLE_SRC = tests/oracle.c
ORACLE_DEP = $(ORACLE_SRC) tests/oracle.h $(LIB_SRC) $(LIB_HDR)
POSIX = -D_POSIX_C_SOURCE=200809L

# make check: the same tests under AddressSanitizer and UndefinedBehaviorSanitizer.
SANITIZE = -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1

# make fuzz: libFuzzer needs clang. FUZZ_TIME is in seconds.
CLANG ?= clang
FUZZ_TIME ?= 60

.PHONY: all test extended check check-extended fuzz clean
all: hw1

hw1: src/main.c $(LIB_SRC) $(LIB_HDR)
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) src/main.c $(LIB_SRC) -o $@

build/run: tests/test.c tests/cases.h $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) tests/test.c $(LIB_SRC) -o $@

build/automata: tests/automata.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) tests/automata.c $(LIB_SRC) -o $@

build/check/automata: tests/automata.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build/check
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) tests/automata.c $(LIB_SRC) -o $@

test: build/run build/automata
	./build/run
	./build/automata

build/differential: tests/differential.c $(ORACLE_DEP)
	@mkdir -p build
	$(CC) $(INCLUDES) $(POSIX) $(CPPFLAGS) $(CFLAGS) tests/differential.c $(ORACLE_SRC) $(LIB_SRC) -o $@

extended: test build/differential
	./build/differential

build/check/run: tests/test.c tests/cases.h $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build/check
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) tests/test.c $(LIB_SRC) -o $@

build/check/differential: tests/differential.c $(ORACLE_DEP)
	@mkdir -p build/check
	$(CC) $(INCLUDES) $(POSIX) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) tests/differential.c $(ORACLE_SRC) $(LIB_SRC) -o $@

check: build/check/run build/check/automata
	./build/check/run
	./build/check/automata

check-extended: check build/check/differential
	./build/check/differential

build/fuzz: tests/fuzz.c $(ORACLE_DEP)
	@mkdir -p build
	$(CLANG) $(INCLUDES) $(POSIX) $(CPPFLAGS) -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined \
		tests/fuzz.c $(ORACLE_SRC) $(LIB_SRC) -o $@

fuzz: build/fuzz
	@mkdir -p build/fuzz-corpus
	cp example.funny build/fuzz-corpus/
	./build/fuzz -max_total_time=$(FUZZ_TIME) -max_len=256 build/fuzz-corpus

clean:
	rm -rf hw1 build
