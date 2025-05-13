#include "vm/frame.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"

static struct list frame_table;
static struct lock frame_lock;
static struct list_elem* ptr = NULL;

/** Init frame_table list and frame_lock. */
void frame_table_init (void) {
    list_init(&frame_table);
    lock_init(&frame_lock);
}

/** Returns whether a frame is dirty. */
static bool is_frame_dirty(struct frame_table_entry* fte) {
    struct thread* t = thread_current();
    return pagedir_is_dirty(t->pagedir, fte->frame);
}

/** Returns whether a frame is accessed. */
static bool is_page_accessed(struct frame_table_entry* fte) {
    struct thread* t = thread_current();
    return pagedir_is_accessed(t->pagedir, fte->frame);
}

/** Set the accessed bit of a frame. */
static void set_page_accessed(struct frame_table_entry* fte, bool accessed) {
    struct thread* t = thread_current();
    pagedir_set_accessed(t->pagedir, fte->frame, accessed);
}

/** Choose a victim to be swapped out.
 *  Use FIFO strategy. Not adapted.
 */
/*
static struct frame_table_entry* pick_victim_fifo(void) {
    struct list_elem* e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        struct frame_table_entry* fte = list_entry(e, struct frame_table_entry, elem);
        if (fte->pinned == false)  return fte;
    }
    return NULL;
}
*/

/** Choose a victim to be swapped out.
 *  Use clock algorithm.
 */
static struct frame_table_entry* pick_victim(void) {
    int flag = 0;
    while (1) {
        /* Deal with details to make it a circle list. */
        if (ptr == NULL || ptr == list_end(&frame_table)) {
            ptr = list_begin(&frame_table);
            /* If the frame table is scanned at least 2 rounds but 
               haven't found available frame to be swapped out, 
               there's no need to go on! */
            flag++;
            if (flag == 3)  return NULL;
            continue;
        }

        struct frame_table_entry* fte = list_entry(ptr, struct frame_table_entry, elem);
        ptr = list_next(ptr);

        /* Pinned frames is not available. */
        if (fte->pinned == true)  continue;

        if (is_page_accessed(fte) == false)  return fte;
        set_page_accessed(fte, false);
    }
}

/** Evict one victim page when pages are not enough. */
static void frame_evict(void) {
    struct frame_table_entry* victim = pick_victim();
    if (victim == NULL)  return;

    /* If a frame is dirty, write back to swap slot and record the slot index. */
    if (is_frame_dirty(victim)) {
        int slot = swap_out(victim->frame);
        if (slot == SWAP_ERROR)  PANIC("Swap out error!");
        victim->spte->slot = slot;
    }

    /* Remove the previous mapping in page directory. */
    pagedir_clear_page(victim->thread->pagedir, victim->spte->vaddr);

    /* Free relevant data structure. */
    palloc_free_page(victim->frame);
    if (ptr == &victim->elem) {
        ptr = list_next(ptr);
        if (ptr == list_end(&frame_table))
            ptr = list_begin(&frame_table);
    } 
    list_remove(&victim->elem);
    free(victim);
}

/** Alloc a frame and record the relevant information. */
void *frame_alloc (enum palloc_flags flags, struct sup_page_table_entry* spte, bool pinned) {
    lock_acquire(&frame_lock);

    /* Ensure that allocation takes place in user space. */
    ASSERT(flags & PAL_USER);

    /* Alloc a frame. */
    void* frame = palloc_get_page(flags);
    if (frame == NULL) {
        /* If the first attempt fails, try to evict another frame. */
        frame_evict();
        frame = palloc_get_page(flags);
        if (frame == NULL) {
            lock_release(&frame_lock);
            return NULL;
        }
    }
    
    /* Record relevant information. */
    struct frame_table_entry* fte = malloc(sizeof(struct frame_table_entry));
    if (fte == NULL) {
        palloc_free_page(frame);
        lock_release(&frame_lock);
        return NULL;
    }

    fte -> frame = frame;
    fte -> spte = spte;
    fte -> pinned = pinned;
    fte -> thread = thread_current();
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
            if (ptr == e) {
                ptr = list_next(ptr);
                if (ptr == list_end(&frame_table))
                    ptr = list_begin(&frame_table);
            } 
            list_remove(e);
            free(fte);
            lock_release(&frame_lock);
            return;
        }
    }

    lock_release(&frame_lock);
    PANIC("Tried to free an unallocated frame!");
}

/* Let a frame able to be swapped out. */
void frame_depin(void* frame) {
    lock_acquire(&frame_lock);

    struct list_elem* e;
    for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
        struct frame_table_entry* fte = list_entry(e, struct frame_table_entry, elem);
        if (fte -> frame == frame) {
            fte->pinned = false;
            lock_release(&frame_lock);
            return;
        }
    }

    lock_release(&frame_lock);
    PANIC("Tried to depin an unallocated page!");
}