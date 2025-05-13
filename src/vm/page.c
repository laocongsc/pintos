#include "vm/page.h"

/** Init the supplemental page table. */
void sup_page_table_init(struct list* sup_page_table_addr) {
    list_init(sup_page_table_addr);
}
