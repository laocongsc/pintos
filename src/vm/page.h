#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "filesys/file.h"

struct sup_page_table_entry{
    void* vaddr;                 /* virtual address */
    bool is_loaded;              /* whether the page has been loaded */
    struct file* file;           /* source file */
    off_t file_offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;               /* whether the page is writable */
    struct list_elem elem;
    void* frame;                 /* the frame allocated to the page */
    int slot;                    /* the swap slot index */
};

/* Init the supplemental page table. */
void sup_page_table_init(struct list* sup_page_table_addr);
