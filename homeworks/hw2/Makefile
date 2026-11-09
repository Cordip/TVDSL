CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
PYTHON ?= python3

# The lexer is hw1, built from its sources; its CLI (src/main.c) is left out.
HW1 = ../hw1
INCLUDES = -Iinclude -Isrc -I$(HW1)/include -I$(HW1)/src

LIB_SRC = $(wildcard src/*/*.c) $(wildcard $(HW1)/src/*/*.c)
LIB_HDR = $(wildcard include/*.h src/*/*.h $(HW1)/include/*.h $(HW1)/src/*/*.h)

# make check: the same tests under AddressSanitizer and UndefinedBehaviorSanitizer.
SANITIZE = -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1

# Bundled course examples; EXAMPLES may point to another copy.
EXAMPLES ?= tests/examples

.PHONY: all test check clean ll1 test-python
all: hw2

hw2: src/main.c $(LIB_SRC) $(LIB_HDR)
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) src/main.c $(LIB_SRC) -o $@

build/check/hw2: src/main.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build/check
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) src/main.c $(LIB_SRC) -o $@

build/garble: tests/garble.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) tests/garble.c $(LIB_SRC) -o $@

build/check/garble: tests/garble.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build/check
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) tests/garble.c $(LIB_SRC) -o $@

build/unit: tests/unit.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) tests/unit.c $(LIB_SRC) -o $@

build/check/unit: tests/unit.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build/check
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) tests/unit.c $(LIB_SRC) -o $@

build/api-example: examples/embed.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) examples/embed.c $(LIB_SRC) -o $@

build/check/api-example: examples/embed.c $(LIB_SRC) $(LIB_HDR)
	@mkdir -p build/check
	$(CC) $(INCLUDES) $(CPPFLAGS) $(CFLAGS) $(SANITIZE) examples/embed.c $(LIB_SRC) -o $@

# Every prefix and one-byte deletion of each case must parse or fail, not hang.
GARBLE_INPUT = tests/cases/*.funny $(wildcard $(EXAMPLES)/*/*.funny)

ll1:
	$(PYTHON) -B tools/ll1.py grammar.md

test-python:
	$(PYTHON) -B -m unittest discover -s tests -p 'test_*.py'

test: test-python hw2 build/unit build/garble build/api-example
	./build/unit
	$(PYTHON) -B tests/run.py ./hw2 "$(EXAMPLES)" --api-example ./build/api-example
	@timeout 60 ./build/garble $(GARBLE_INPUT) 2>/dev/null

check: test-python build/check/hw2 build/check/unit build/check/garble build/check/api-example
	./build/check/unit
	$(PYTHON) -B tests/run.py ./build/check/hw2 "$(EXAMPLES)" --api-example ./build/check/api-example
	@timeout 600 ./build/check/garble $(GARBLE_INPUT) 2>/dev/null

clean:
	rm -rf hw2 build
