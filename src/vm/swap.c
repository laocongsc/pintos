#include "vm/swap.h"
#include "devices/block.h"
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

#define K PGSIZE / BLOCK_SECTOR_SIZE

struct block* swap_slot;
block_sector_t slot_count;
struct bitmap* swap_map;
struct lock swap_lock;

/** Get the swap block device and update its size.
 *  Create bitmap and set all bits usable.
 *  Init swap_lock used in this part.
 */
void swap_slot_init(void) {
    swap_slot = block_get_role(BLOCK_SWAP);
    if (swap_slot == NULL)
        PANIC("Swap slot not exists!");

    slot_count = block_size(swap_slot);
    swap_map = bitmap_create(slot_count);
    bitmap_set_all(swap_map, 0);
    lock_init(&swap_lock);
}

/** Swap out pages and record the bits used in bitmap. */
int swap_out(void* frame) {
    lock_acquire(&swap_lock);

    /* Find K continuous usable bits and set all of them being used. */
    block_sector_t pos = bitmap_scan_and_flip(swap_map, 0, K, 0);
    if (pos == BITMAP_ERROR) {
        lock_release(&swap_lock);
        return SWAP_ERROR;
    }
    
    /* Write contents into swap slot page by page. */
    for (int offset = 0, cnt = 0; offset < PGSIZE; offset += BLOCK_SECTOR_SIZE, cnt++)
        block_write(swap_slot, pos + cnt, (char*)frame + offset);
    lock_release(&swap_lock);
    return pos;
}

/** Swap in pages and update the bitmap. */
void swap_in(int pos, void* frame) {
    lock_acquire(&swap_lock);

    /* Ensure that pos is valid. */
    ASSERT(pos >= 0 && pos + K <= slot_count);

    /* Read contents from swap slot page by page. */
    for (int offset = 0, cnt = 0; offset < PGSIZE; offset += BLOCK_SECTOR_SIZE, cnt++)
        block_read(swap_slot, pos + cnt, frame + offset);

    /* Set all K bits usable. */
    bitmap_set_multiple(swap_map, pos, K, 0);

    lock_release(&swap_lock);
}

/* Set K continuous bits from pos usable. */
void swap_set(int pos) {
    lock_acquire(&swap_lock);

    /* Ensure that pos is valid and set all K bits usable. */
    ASSERT(pos >= 0 && pos + K <= slot_count);
    bitmap_set_multiple(swap_map, pos, K, 0);

    lock_release(&swap_lock);
}
