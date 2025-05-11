#include "vm/frame.h"
#include "threads/malloc.h"

/** Init frame_table list and frame_lock. */
void frame_table_init (void) {
    list_init(&frame_table);
    lock_init(&frame_lock);
}

/** Alloc a frame and record the relevant information. */
void *frame_alloc (enum palloc_flags flags, struct page *page) {
    lock_acquire(&frame_lock);

    ASSERT(flags & PAL_USER);
    void* frame = palloc_get_page(flags);
    if (frame == NULL) {
        lock_release(&frame_lock);
        return NULL;
    }

    struct frame_table_entry* fte = malloc(sizeof(struct frame_table_entry));
    if (fte == NULL) {
        palloc_free_page(frame);
        lock_release(&frame_lock);
        return NULL;
    }

    fte -> frame = frame;
    fte -> page = page;
    fte -> pinned = 0;
    list_push_back(&frame_table, &fte -> elem);

    lock_release(&frame_lock);
    return frame;
}

/** Free a frame. */
void frame_free (void *frame) {
    lock_acquire(&frame_lock);

    struct list_elem* e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        struct frame_table_entry* fte = list_entry(e, struct frame_table_entry, elem);
        if (fte -> frame == frame) {
            palloc_free_page(frame);
            list_remove(e);
            free(fte);
            lock_release(&frame_lock);
            return;
        }
    }

    lock_release(&frame_lock);
    PANIC("Tried to free an unallocated frame!");
}