/* Shared by all modules: the input alphabet and allocation helpers. */
#ifndef UTIL_COMMON_H
#define UTIL_COMMON_H

#include <stddef.h>

/* The language spec fixes the alphabet to ASCII. Every byte outside it is one
   extra symbol, NON_ASCII, which only a negated class matches: that lets
   comments hold any text while a non-ASCII byte elsewhere goes to the trap. */
#define ASCII 128
#define NON_ASCII ASCII
#define ALPHABET (ASCII + 1)
#define MASK_WORDS ((ALPHABET + 63) / 64)

static inline int symbol_of(unsigned char byte) {
  return byte < ASCII ? byte : NON_ASCII;
}

/* Allocation failure is fatal: the automaton cannot be built without memory. */
void *xrealloc(void *ptr, size_t size);
void *xcalloc(size_t count, size_t size);

#endif
