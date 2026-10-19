/* Hopcroft minimization.
 *
 * Blocks are candidate equivalence classes. A work item (block, character) is
 * a splitter: a state that goes into `block` on `character` cannot be
 * equivalent to one that does not, so every block mixing both kinds must be
 * split.
 *
 * The data layout keeps every step proportional to the work it does:
 *   - inverse transitions list the predecessors of each (character, state),
 *     so a splitter looks only at the transitions that enter its block;
 *   - every block is a contiguous slice of `elements`, and marked states are
 *     swapped to the front of their slice, so marking and splitting cost O(1)
 *     per state touched;
 *   - after a split only the smaller half is queued; the larger half stays
 *     queued only when the block was already pending. Each state therefore
 *     lands in a queued block at most log2(n) times per character, which
 *     gives the O(|Σ| · n · log n) bound of Hopcroft's algorithm. */

#include "automata/minimize.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  int block, character;
} Work;

/* elements[first .. first + size) holds the block; its first `marked`
   entries are the states marked by the splitter being processed. */
typedef struct {
  int first, size, marked;
} Block;

typedef struct {
  int state_count;
  int *elements, *location, *block_of;
  Block *blocks;
  int block_count;
  /* Predecessors of (character, state) are
     inverse_list[inverse_start[key] .. inverse_start[key + 1]) with
     key = character * state_count + state. */
  int *inverse_start, *inverse_list;
  int *touched, touched_count;
  Work *queue;
  size_t head, count, capacity;
  unsigned char *pending;
} Partition;

static void build_inverse(Partition *partition, const Dfa *source) {
  int state_count = partition->state_count;
  size_t slots = (size_t)ALPHABET * (size_t)state_count;

  partition->inverse_start =
      xcalloc(slots + 1, sizeof *partition->inverse_start);
  partition->inverse_list =
      xrealloc(NULL, slots * sizeof *partition->inverse_list);

  for (int state = 0; state < state_count; ++state) {
    for (int character = 0; character < ALPHABET; ++character) {
      size_t key = (size_t)character * (size_t)state_count +
                   (size_t)source->states[state].next[character];

      ++partition->inverse_start[key + 1];
    }
  }

  for (size_t key = 1; key <= slots; ++key) {
    partition->inverse_start[key] += partition->inverse_start[key - 1];
  }

  int *cursor = xrealloc(NULL, slots * sizeof *cursor);

  memcpy(cursor, partition->inverse_start, slots * sizeof *cursor);

  for (int state = 0; state < state_count; ++state) {
    for (int character = 0; character < ALPHABET; ++character) {
      size_t key = (size_t)character * (size_t)state_count +
                   (size_t)source->states[state].next[character];

      partition->inverse_list[cursor[key]++] = state;
    }
  }

  free(cursor);
}

/* Acceptance classes must remain distinct even if their transitions match. */
static void init_blocks(Partition *partition, const Dfa *source) {
  int state_count = partition->state_count;
  int *label = xrealloc(NULL, (size_t)state_count * sizeof *label);

  for (int state = 0; state < state_count; ++state) {
    int block_index;

    for (block_index = 0; block_index < partition->block_count; ++block_index) {
      if (label[block_index] == source->states[state].accept) {
        break;
      }
    }

    if (block_index == partition->block_count) {
      label[partition->block_count++] = source->states[state].accept;
    }

    partition->block_of[state] = block_index;
    ++partition->blocks[block_index].size;
  }

  int first = 0;

  for (int block_index = 0; block_index < partition->block_count;
       ++block_index) {
    partition->blocks[block_index].first = first;
    first += partition->blocks[block_index].size;
    partition->blocks[block_index].size = 0;
  }

  for (int state = 0; state < state_count; ++state) {
    Block *block = &partition->blocks[partition->block_of[state]];
    int position = block->first + block->size++;

    partition->elements[position] = state;
    partition->location[state] = position;
  }

  free(label);
}

static void enqueue(Partition *partition, int block, int character) {
  size_t key = (size_t)block * ALPHABET + (size_t)character;

  if (partition->pending[key]) {
    return;
  }

  partition->pending[key] = 1;

  if (partition->count == partition->capacity) {
    partition->capacity = partition->capacity ? partition->capacity * 2 : 256;
    partition->queue = xrealloc(partition->queue,
                                partition->capacity * sizeof *partition->queue);
  }

  partition->queue[partition->count++] = (Work){block, character};
}

/* Swaps the state into the marked prefix of its block. */
static void mark(Partition *partition, int state) {
  int block_index = partition->block_of[state];
  Block *block = &partition->blocks[block_index];
  int position = partition->location[state];
  int target = block->first + block->marked;

  if (position < target) {
    return;
  }

  int displaced = partition->elements[target];

  partition->elements[target] = state;
  partition->elements[position] = displaced;
  partition->location[state] = target;
  partition->location[displaced] = position;

  if (block->marked == 0) {
    partition->touched[partition->touched_count++] = block_index;
  }

  ++block->marked;
}

/* Every touched block with both marked and unmarked states is cut in two.
   The smaller half becomes a new block and is queued for every character;
   if the old block is still pending, both halves are pending as required. */
static void split_touched(Partition *partition) {
  for (int touched_index = 0; touched_index < partition->touched_count;
       ++touched_index) {
    int block_index = partition->touched[touched_index];
    Block *block = &partition->blocks[block_index];
    int unmarked = block->size - block->marked;

    if (block->marked > 0 && unmarked > 0) {
      int fresh_index = partition->block_count++;
      Block *fresh = &partition->blocks[fresh_index];

      if (block->marked <= unmarked) {
        *fresh = (Block){block->first, block->marked, 0};
        block->first += block->marked;
        block->size = unmarked;
      } else {
        *fresh = (Block){block->first + block->marked, unmarked, 0};
        block->size = block->marked;
      }

      for (int position = fresh->first; position < fresh->first + fresh->size;
           ++position) {
        partition->block_of[partition->elements[position]] = fresh_index;
      }

      for (int character = 0; character < ALPHABET; ++character) {
        enqueue(partition, fresh_index, character);
      }
    }

    block->marked = 0;
  }

  partition->touched_count = 0;
}

static Dfa build_result(const Partition *partition, const Dfa *source) {
  Dfa result = {0};

  result.count = result.capacity = (size_t)partition->block_count;
  result.states = xcalloc(result.count, sizeof *result.states);
  result.start = partition->block_of[source->start];
  result.trap = source->trap < 0 ? -1 : partition->block_of[source->trap];

  for (int state = 0; state < partition->state_count; ++state) {
    DfaState *target = &result.states[partition->block_of[state]];

    target->accept = source->states[state].accept;

    for (int character = 0; character < ALPHABET; ++character) {
      target->next[character] =
          partition->block_of[source->states[state].next[character]];
    }
  }

  return result;
}

Dfa minimize(const Dfa *source) {
  Partition partition = {.state_count = (int)source->count};
  size_t state_count = source->count;

  partition.elements = xrealloc(NULL, state_count * sizeof *partition.elements);
  partition.location = xrealloc(NULL, state_count * sizeof *partition.location);
  partition.block_of = xrealloc(NULL, state_count * sizeof *partition.block_of);
  partition.blocks = xcalloc(state_count, sizeof *partition.blocks);
  partition.touched = xrealloc(NULL, state_count * sizeof *partition.touched);
  partition.pending = xcalloc(state_count * ALPHABET, 1);

  build_inverse(&partition, source);
  init_blocks(&partition, source);

  for (int block_index = 0; block_index < partition.block_count;
       ++block_index) {
    for (int character = 0; character < ALPHABET; ++character) {
      enqueue(&partition, block_index, character);
    }
  }

  /* The splitter's slice may be reordered while its predecessors are marked,
     so its members are copied before the walk. */
  int *members = xrealloc(NULL, state_count * sizeof *members);

  while (partition.head < partition.count) {
    Work work = partition.queue[partition.head++];
    Block *splitter = &partition.blocks[work.block];
    int member_count = splitter->size;

    partition.pending[(size_t)work.block * ALPHABET + (size_t)work.character] =
        0;
    memcpy(members, partition.elements + splitter->first,
           (size_t)member_count * sizeof *members);

    for (int member_index = 0; member_index < member_count; ++member_index) {
      size_t key =
          (size_t)work.character * state_count + (size_t)members[member_index];

      for (int index = partition.inverse_start[key];
           index < partition.inverse_start[key + 1]; ++index) {
        mark(&partition, partition.inverse_list[index]);
      }
    }

    split_touched(&partition);
  }

  Dfa result = build_result(&partition, source);

  free(members);
  free(partition.elements);
  free(partition.location);
  free(partition.block_of);
  free(partition.blocks);
  free(partition.touched);
  free(partition.pending);
  free(partition.inverse_start);
  free(partition.inverse_list);
  free(partition.queue);

  return result;
}
