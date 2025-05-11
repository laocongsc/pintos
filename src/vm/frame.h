#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/palloc.h"

struct frame_table_entry{
    void* frame;
    struct page* page;
    bool pinned;
    struct list_elem elem;
};

static struct list frame_table;
static struct lock frame_lock;

void frame_table_init (void);
void *frame_alloc (enum palloc_flags flags, struct page *page);
void frame_free (void *frame);