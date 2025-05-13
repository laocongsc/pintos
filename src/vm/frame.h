#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "threads/thread.h"

struct frame_table_entry
{
    void *frame;                        /* frame address */
    struct sup_page_table_entry *spte;  /* relevant information in supplemental page table */
    bool pinned;                        /* whether it can be swapped out */
    struct thread* thread;              /* the thread owning the frame */
    struct list_elem elem;
};

/** Init the frame table. */
void frame_table_init(void);

/** Alloc and free frames. */
void *frame_alloc (enum palloc_flags flags, struct sup_page_table_entry* spte, bool pinned);
void frame_free(void *frame);

/** Let the frame able to be swapped out. */
void frame_depin(void *frame);