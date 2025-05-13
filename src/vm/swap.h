#include <debug.h>
#include <list.h>
#include <stdint.h>

#define SWAP_ERROR -1
#define SWAP_NONE -1

/** Init data structures and constants about swap slot. */
void swap_slot_init(void);

/** Swap out and in pages. */
int swap_out(void* frame);
void swap_in(int pos, void* frame);

/** Set pages usable in swap slot. */
void swap_set(int pos);
