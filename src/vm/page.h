#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "filesys/file.h"

struct page{
    void* addr;
};

struct sup_page_table_entry{
    void* vaddr;
    bool is_loaded;
    struct file* file;
    off_t file_offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;
    struct list_elem elem;
};

void sup_page_table_init(struct list* sup_page_table_addr);