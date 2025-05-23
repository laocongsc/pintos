#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/swap.h"

static thread_func start_process NO_RETURN;
static bool load(const char *file_name, char *cmdline, void (**eip)(void), void **esp);

/* a struct to pass arguments to start_process */
struct args
{
    char *fn_copy;
    tid_t parent_tid;
    int success;
};

/** Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t process_execute(const char *cmdline)
{
    char *fn_copy, *file_name, *save_ptr;
    tid_t tid;

    /* Make a copy of FILE_NAME, used for argument passing
       Otherwise there's a race between the caller and load(). */
    fn_copy = palloc_get_page(0);
    if (fn_copy == NULL)
        return TID_ERROR;
    strlcpy(fn_copy, cmdline, PGSIZE);

    /* another copy of FILE_NAME, used for thread's name */
    file_name = palloc_get_page(0);
    if (file_name == NULL)
    {
        palloc_free_page(fn_copy);
        return TID_ERROR;
    }
    strlcpy(file_name, cmdline, PGSIZE);
    char *token = strtok_r(file_name, " ", &save_ptr);

    /* for passing arguments to start_process */
    struct args *argss = malloc(sizeof(struct args));
    if (argss == NULL)
    {
        palloc_free_page(fn_copy);
        palloc_free_page(file_name);
        return TID_ERROR;
    }
    argss->fn_copy = fn_copy;
    argss->parent_tid = thread_tid();
    argss->success = 0;

    /* Create a new thread to execute FILE_NAME. */
    tid = thread_create(token, PRI_DEFAULT + 5, start_process, argss);
    palloc_free_page(file_name);
    if (tid == TID_ERROR)
    {
        palloc_free_page(fn_copy);
        free(argss);
        return TID_ERROR;
    }

    /* Ensure that the parent knows whether its child successfully loads */
    enum intr_level old_level;
    old_level = intr_disable();
    if (((argss->success) >> 1) == 0)
        thread_block();
    intr_set_level(old_level);

    if ((argss->success & 1) == 0)
        tid = TID_ERROR;
    free(argss);

    return tid;
}

/** A thread function that loads a user process and starts it
   running. */
static void
start_process(void *argss)
{
    char *cmdline = ((struct args *)argss)->fn_copy;
    tid_t parent_tid = ((struct args *)argss)->parent_tid;
    struct intr_frame if_;
    bool success;

    /* Initialize information of child process */
    struct thread *child = thread_current();
    struct thread *parent = get_thread(parent_tid);
    child->parent = parent_tid;
    child->exit_status = -1;
    memset(child->all_files, 0, sizeof(child->all_files));
    sema_init(&child->s, 0);
    child->exe = NULL;
    sup_page_table_init(&child->sup_page_table);

    /* Initialize interrupt frame and load executable. */
    memset(&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    success = load(thread_current()->name, cmdline, &if_.eip, &if_.esp);
    ((struct args *)argss)->success = (int)success + 2;

    /* Unblock its parent process to inform it the load status */
    enum intr_level old_level;
    old_level = intr_disable();
    if (parent->status == THREAD_BLOCKED)
        thread_unblock(parent);
    intr_set_level(old_level);

    palloc_free_page(cmdline);

    /* return to normal priority */
    thread_set_priority(PRI_DEFAULT);

    if (!success)
        thread_exit();

    /* Start the user process by simulating a return from an
       interrupt, implemented by intr_exit (in
       threads/intr-stubs.S).  Because intr_exit takes all of its
       arguments on the stack in the form of a `struct intr_frame',
       we just point the stack pointer (%esp) to our stack frame
       and jump to it. */
    asm volatile("movl %0, %%esp; jmp intr_exit" : : "g"(&if_) : "memory");
    NOT_REACHED();
}

/* Find a child according to its tid in list dead_children */
static struct exec_info *find_child(struct list *l, tid_t child_tid)
{
    struct list_elem *e;
    for (e = list_begin(l); e != list_end(l); e = list_next(e))
    {
        struct exec_info *info = list_entry(e, struct exec_info, elem);
        if (info->tid == child_tid)
            return info;
    }
    return NULL;
}

/** Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
/** Astonishingly, there's no problem 2-2 in pintos book! */
int process_wait(tid_t child_tid)
{
    struct exec_info *child_info = find_child(&thread_current()->dead_children, child_tid);
    if (child_info == NULL)
    {
        /** Its child has not died. */
        struct thread *child = get_thread(child_tid);
        /** check if it is a dead child from other family */
        if (child == NULL)
            return -1;
        /** check if it is a live child from other family */
        if (child->parent != thread_tid())
            return -1;
        /** Now, we guarantee that it's our own child. */
        sema_down(&child->s);
        child_info = find_child(&thread_current()->dead_children, child_tid);
    }
    /** Its child has already dead now. */
    int val = child_info->exit_status;
    list_remove(&child_info->elem);
    free(child_info);
    return val;
}

/** Free the current process's resources. */
void process_exit(void)
{
    struct thread *cur = thread_current();
    uint32_t *pd;

    /* print termination messages */
    printf("%s: exit(%d)\n", cur->name, cur->exit_status);

    file_close(cur->exe);
    cur->exe = NULL;

    /* Destroy the current process's page directory and switch back
       to the kernel-only page directory. */
    pd = cur->pagedir;
    if (pd != NULL)
    {
        /* Correct ordering here is crucial.  We must set
           cur->pagedir to NULL before switching page directories,
           so that a timer interrupt can't switch back to the
           process page directory.  We must activate the base page
           directory before destroying the process's page
           directory, or our active page directory will be one
           that's been freed (and cleared). */
        cur->pagedir = NULL;

        while (!list_empty(&cur->sup_page_table))
        {
            struct list_elem *e = list_pop_front(&cur->sup_page_table);
            struct sup_page_table_entry *spte = list_entry(e, struct sup_page_table_entry, elem);
            if (spte->is_loaded) {
                if (spte->slot == SWAP_NONE)
                    frame_free(spte->frame);
                else  swap_set(spte->slot);
            }
            free(spte);
        }

        pagedir_activate(NULL);
        pagedir_destroy(pd);
    }
}

/** Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void process_activate(void)
{
    struct thread *t = thread_current();

    /* Activate thread's page tables. */
    pagedir_activate(t->pagedir);

    /* Set thread's kernel stack for use in processing
       interrupts. */
    tss_update();
}

/** We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/** ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/** For use with ELF types in printf(). */
#define PE32Wx PRIx32 /**< Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32 /**< Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32 /**< Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16 /**< Print Elf32_Half in hexadecimal. */

/** Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
    unsigned char e_ident[16];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off e_phoff;
    Elf32_Off e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
};

/** Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
    Elf32_Word p_type;
    Elf32_Off p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

/** Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL 0           /**< Ignore. */
#define PT_LOAD 1           /**< Loadable segment. */
#define PT_DYNAMIC 2        /**< Dynamic linking info. */
#define PT_INTERP 3         /**< Name of dynamic loader. */
#define PT_NOTE 4           /**< Auxiliary info. */
#define PT_SHLIB 5          /**< Reserved. */
#define PT_PHDR 6           /**< Program header table. */
#define PT_STACK 0x6474e551 /**< Stack segment. */

/** Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1 /**< Executable. */
#define PF_W 2 /**< Writable. */
#define PF_R 4 /**< Readable. */

static bool setup_stack(void **esp);
static bool validate_segment(const struct Elf32_Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
                         uint32_t read_bytes, uint32_t zero_bytes,
                         bool writable);

/** Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool load(const char *file_name, char *cmdline, void (**eip)(void), void **esp)
{
    struct thread *t = thread_current();
    struct Elf32_Ehdr ehdr;
    struct file *file = NULL;
    off_t file_ofs;
    bool success = false;
    int i;

    /* Allocate and activate page directory. */
    t->pagedir = pagedir_create();
    if (t->pagedir == NULL)
        goto done;
    process_activate();

    /* Open executable file. */
    file = filesys_open(file_name);
    if (file == NULL)
    {
        printf("load: %s: open failed\n", file_name);
        goto done;
    }
    t->exe = file;
    file_deny_write(file);

    /* Read and verify executable header. */
    if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\1\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 3 || ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Elf32_Phdr) || ehdr.e_phnum > 1024)
    {
        printf("load: %s: error loading executable\n", file_name);
        goto done;
    }

    /* Read program headers. */
    file_ofs = ehdr.e_phoff;
    for (i = 0; i < ehdr.e_phnum; i++)
    {
        struct Elf32_Phdr phdr;

        if (file_ofs < 0 || file_ofs > file_length(file))
            goto done;
        file_seek(file, file_ofs);

        if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
            goto done;
        file_ofs += sizeof phdr;
        switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
            /* Ignore this segment. */
            break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
            goto done;
        case PT_LOAD:
            if (validate_segment(&phdr, file))
            {
                bool writable = (phdr.p_flags & PF_W) != 0;
                uint32_t file_page = phdr.p_offset & ~PGMASK;
                uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
                uint32_t page_offset = phdr.p_vaddr & PGMASK;
                uint32_t read_bytes, zero_bytes;
                if (phdr.p_filesz > 0)
                {
                    /* Normal segment.
                       Read initial part from disk and zero the rest. */
                    read_bytes = page_offset + phdr.p_filesz;
                    zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
                }
                else
                {
                    /* Entirely zero.
                       Don't read anything from disk. */
                    read_bytes = 0;
                    zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
                }
                if (!load_segment(file, file_page, (void *)mem_page,
                                  read_bytes, zero_bytes, writable))
                    goto done;
            }
            else
                goto done;
            break;
        }
    }

    /* Divide command line. */
    char **argv = palloc_get_page(0);
    char *token, *save_ptr;
    size_t argc = 0;
    if (argv == NULL)
        goto done;
    size_t len = strlen(cmdline) + 1;
    for (token = strtok_r(cmdline, " ", &save_ptr); token != NULL;
         token = strtok_r(NULL, " ", &save_ptr))
    {
        *(argv + argc) = token;
        argc++;
    }

    /* Set up stack. */
    if (!setup_stack(esp))
        goto done;

    /* Start address. */
    *eip = (void (*)(void))ehdr.e_entry;

    /* Put the arguments for the initial function on the stack. */
    /* put cmdline (argv[][]) on the stack */
    *esp -= len;
    memcpy(*esp, cmdline, len);

    /* deal with argv[] (update address) */
    for (size_t i = 0, delta = (size_t)(*esp) - (size_t)cmdline; i < argc; i++)
        *(argv + i) += delta;
    *(argv + argc) = 0;
    while ((size_t)(*esp) % 4 != 0)
        (*esp)--;

    /* put argv[] on the stack */
    len = (argc + 1) * 4;
    *esp -= len;
    memcpy(*esp, (char *)argv, len);

    /* store ARGV, ARGC and fake return address */
    *(size_t *)(*esp - 4) = *(size_t *)(esp);
    *(size_t *)(*esp - 8) = argc;
    *(size_t *)(*esp - 12) = 0;
    *esp -= 12;

    palloc_free_page(argv);

    success = true;

done:
    /* We arrive here whether the load is successful or not. */
    return success;
}

/** Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Elf32_Phdr *phdr, struct file *file)
{
    /* p_offset and p_vaddr must have the same page offset. */
    if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
        return false;

    /* p_offset must point within FILE. */
    if (phdr->p_offset > (Elf32_Off)file_length(file))
        return false;

    /* p_memsz must be at least as big as p_filesz. */
    if (phdr->p_memsz < phdr->p_filesz)
        return false;

    /* The segment must not be empty. */
    if (phdr->p_memsz == 0)
        return false;

    /* The virtual memory region must both start and end within the
       user address space range. */
    if (!is_user_vaddr((void *)phdr->p_vaddr))
        return false;
    if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
        return false;

    /* The region cannot "wrap around" across the kernel virtual
       address space. */
    if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
        return false;

    /* Disallow mapping page 0.
       Not only is it a bad idea to map page 0, but if we allowed
       it then user code that passed a null pointer to system calls
       could quite likely panic the kernel by way of null pointer
       assertions in memcpy(), etc. */
    if (phdr->p_vaddr < PGSIZE)
        return false;

    /* It's okay. */
    return true;
}

/** Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
             uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    file_seek(file, ofs);
    while (read_bytes > 0 || zero_bytes > 0)
    {
        /* Calculate how to fill this page.
           We will read PAGE_READ_BYTES bytes from FILE
           and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        /* For lazy loading, we just record relevant information in 
           supplemental page table, but not actually load the page. */
        struct sup_page_table_entry *spte = malloc(sizeof(struct sup_page_table_entry));
        if (spte == NULL)
            return false;
        spte->vaddr = upage;
        spte->is_loaded = false;
        spte->file = file;
        spte->file_offset = ofs;
        spte->read_bytes = page_read_bytes;
        spte->zero_bytes = page_zero_bytes;
        spte->writable = writable;
        spte->frame = NULL;
        spte->slot = SWAP_NONE;
        list_push_back(&thread_current()->sup_page_table, &spte->elem);

        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
        ofs += PGSIZE;
    }
    return true;
}

/** Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack(void **esp)
{
    /* For lazy loading, we just record relevant information in 
       supplemental page table, but not actually load the page. */
    struct sup_page_table_entry *spte = malloc(sizeof(struct sup_page_table_entry));
    if (spte == NULL)
        return false;

    spte->vaddr = ((uint8_t *)PHYS_BASE) - PGSIZE;
    spte->is_loaded = false;
    spte->file = NULL;
    spte->writable = true;
    spte->frame = NULL;
    spte->slot = SWAP_NONE;
    list_push_back(&thread_current()->sup_page_table, &spte->elem);
    *esp = PHYS_BASE;
    return true;
}
