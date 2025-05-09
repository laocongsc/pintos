#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *f);
static void close(int fd);
static void assert_pointer(void* pointer);

static int next_fd = 2;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/** Halt the operating system. */
static void halt(void) {
  shutdown_power_off();
}

/** Terminate this process. */
static void exit(int status) {
  struct thread* t = thread_current();
  t -> exit_status = status;
  
  /* If its parent process is still alive, 
     put the child's status into the parent process's dead_children list. */
  enum intr_level old_level;
  old_level = intr_disable ();
  struct thread* parent = get_thread(t -> parent);
  if (parent != NULL) {
    struct exec_info* info = malloc(sizeof(struct exec_info));
    if (info != NULL) {
      info -> tid = t -> tid;
      info -> exit_status = status;
      list_push_back(&parent -> dead_children, &info -> elem);
      sema_up(&t -> s);
    }
  }
  intr_set_level (old_level);
  
  /* Release the elements in dead_children list */
  while (!list_empty(&t -> dead_children)) {
    struct list_elem* e = list_pop_front(&t -> dead_children);
    struct exec_info* info = list_entry(e, struct exec_info, elem);
    free(info);
  }

  /* Close the opened files. */
  for (int fd = 2; fd < MAX_FD; fd++)
    if (t -> all_files[fd])  close(fd);
  thread_exit();
}

/** Start another process. */
static tid_t exec(const char *cmd_line) {
  tid_t tid = process_execute(cmd_line);
  return tid;
}

/** Wait for a child process to die. */
static int wait(tid_t tid) {
  return process_wait(tid);
}

/** Create a file. */
static bool create(const char *file, unsigned initial_size) {
  if (file == NULL)  return 0;
  return filesys_create(file, initial_size);
}

/** Delete a file. */
static bool remove(const char *file) {
  if (file == NULL)  return 0;
  return filesys_remove(file);
}

/** Open a file. */
static int open(const char *file) {
  if (file == NULL)  return -1;
  if (next_fd >= MAX_FD)  return -1;
  struct file* s = filesys_open(file);
  if (s == NULL)  return -1;
  int fd = next_fd;
  next_fd++;
  thread_current() -> all_files[fd] = s;
  return fd;
}

static bool is_valid_fd(int fd) {
  if (fd >= next_fd)  return 0;
  if (fd < 0)  return 0;
  return 1;
}

/** Obtain a file's size. */
static int filesize(int fd) {
  if (!is_valid_fd(fd))  return 0;
  struct file* s = thread_current() -> all_files[fd];
  return file_length(s);
}

/** Read from a file. */
static int read(int fd, void *buffer, unsigned size) {
  if (fd == 0) {
    for (unsigned int i = 0; i < size; i++)
      *(uint8_t*)(buffer + i) = input_getc();
    return size;
  }
  if (!is_valid_fd(fd))  return 0;
  struct file* s = thread_current() -> all_files[fd];
  if (s == NULL)  return -1;
  return file_read(s, buffer, size); 
}

/** Write to a file. */
static int write(int fd, const void *buffer, unsigned size) {
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  if (!is_valid_fd(fd))  return 0;
  struct file* s = thread_current() -> all_files[fd];
  if (s == NULL)  return -1;
  return file_write(s, buffer, size);
}

/** Change position in a file. */
static void seek(int fd, unsigned position) {
  if (!is_valid_fd(fd))  return;
  struct file* s = thread_current() -> all_files[fd];
  file_seek(s, position);
}

/** Report current position in a file. */
static unsigned tell(int fd) {
  if (!is_valid_fd(fd))  return 0;
  struct file* s = thread_current() -> all_files[fd];
  return file_tell(s);
}

/** Close a file. */
static void close(int fd) {
  if (!is_valid_fd(fd))  return;
  struct thread* t = thread_current();
  struct file* s = t -> all_files[fd];
  t -> all_files[fd] = NULL;
  file_close(s);
}

static void
syscall_handler (struct intr_frame *f) 
{
  assert_pointer(f -> esp);
  uint32_t syscall_num = *(uint32_t*)(f -> esp);
  uint32_t first_arg, second_arg, third_arg;
  uint32_t value = 0;

  switch(syscall_num) {
    case SYS_READ:
    case SYS_WRITE:
      assert_pointer(f -> esp + 12);
      third_arg = *(uint32_t*)(f -> esp + 12);

    case SYS_SEEK:
    case SYS_CREATE:
      assert_pointer(f -> esp + 8);
      second_arg = *(uint32_t*)(f -> esp + 8);

    case SYS_TELL:
    case SYS_CLOSE:
    case SYS_FILESIZE:
    case SYS_OPEN:
    case SYS_EXIT:
    case SYS_EXEC:
    case SYS_WAIT:
    case SYS_REMOVE:
      assert_pointer(f -> esp + 4);
      first_arg = *(uint32_t*)(f -> esp + 4);
  }

  switch(syscall_num) {
    case SYS_HALT:        /**< Halt the operating system. */
      halt();
      break;

    case SYS_EXIT:        /**< Terminate this process. */
      exit((int) first_arg);
      break;

    case SYS_EXEC:        /**< Start another process. */
      assert_pointer((void*) first_arg);
      value = (uint32_t) exec((char*) first_arg);
      break;

    case SYS_WAIT:        /**< Wait for a child process to die. */
      value = (uint32_t) wait((tid_t) first_arg);
      break;

    case SYS_CREATE:      /**< Create a file. */
      assert_pointer((void*) first_arg);
      value = (uint32_t) create((char*) first_arg, (unsigned int) second_arg);
      break;

    case SYS_REMOVE:      /**< Delete a file. */
      assert_pointer((void*) first_arg);
      value = (uint32_t) remove((char*) first_arg);
      break;

    case SYS_OPEN:        /**< Open a file. */
      assert_pointer((void*) first_arg);
      value = (uint32_t) open((char*) first_arg);
      break;

    case SYS_FILESIZE:    /**< Obtain a file's size. */
      value = (uint32_t) filesize((int) first_arg);
      break;

    case SYS_READ:        /**< Read from a file. */
      assert_pointer((void*) second_arg);
      value = (uint32_t) read((int) first_arg, (void*) second_arg, (unsigned int) third_arg);
      break;

    case SYS_WRITE:       /**< Write to a file. */
      assert_pointer((void*) second_arg);
      value = (uint32_t) write((int) first_arg, (void*) second_arg, (unsigned int) third_arg);
      break;

    case SYS_SEEK:        /**< Change position in a file. */
      seek((int) first_arg, (unsigned int) second_arg);
      break;

    case SYS_TELL:        /**< Report current position in a file. */
      value = (uint32_t) tell((int) first_arg);
      break;

    case SYS_CLOSE:       /**< Close a file. */
      close((int) first_arg);
      break;
  }

  f -> eax = value;
}

/** Check whether a pointer is valid. */
static bool is_valid_ptr(void* pointer) {
  if (is_user_vaddr(pointer) == 0)  return 0;
  struct thread* t = thread_current();
  uint32_t *pd = t -> pagedir;
  if (pagedir_get_page(pd, pointer) == NULL)  return 0;
  return 1;
}

/** Verify whether a pointer is valid. If not, exit the process. */
static void assert_pointer(void* pointer) {
  if (is_valid_ptr(pointer) && is_valid_ptr(pointer + 3))  return;
  exit(-1);
}
