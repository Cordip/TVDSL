#include "util/common.h"

#include <stdio.h>
#include <stdlib.h>

void *xrealloc(void *ptr, size_t size) {
  void *result = realloc(ptr, size);

  if (!result) {
    fputs("Out of memory\n", stderr);
    exit(1);
  }

  return result;
}

void *xcalloc(size_t count, size_t size) {
  void *result = calloc(count, size);

  if (!result) {
    fputs("Out of memory\n", stderr);
    exit(1);
  }

  return result;
}
