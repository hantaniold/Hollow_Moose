#ifndef PAGE_H
#define PAGE_H

#include <hash.h>
#include <stdbool.h>
#include "threads/thread.h"
#include "vm/frame.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/block.h"

/* Virtual page. */
struct page {
  /* Immutable members. */
  void *addr;                 /* User virtual address. */
  bool read_only;             /* Read-only page? */
  struct thread *thread;      /* Owning thread. */


  uint32_t read_bytes;        /* for part of executable */
  uint32_t zero_bytes;        /* only use when read_only = true*/
  off_t ofs;
  bool from_exec;

  /* Accessed only in owning process context. */
  struct hash_elem hash_elem; /* struct thread `pages' hash element. */

  /* Set only in owning process context with frame->frame_lock held.           
     Cleared only with scan_lock and frame->frame_lock held. */
  struct frame *frame;        /* Page frame. */

  bool in_memory;
  /* Swap information, protected by frame->frame_lock. */
  block_sector_t sector;       /* Starting sector of swap area, or -1. */

  /* Memory-mapped file information, protected by frame->frame_lock. */
  bool private;               /* False to write back to file,                  
				 true to write back to swap. */
  struct file *file;          /* File. */
  off_t file_offset;          /* Offset in file. */
  off_t file_bytes;           /* Bytes to read/write, 1...PGSIZE. */
  bool mmap;
};

typedef struct page page;

hash_hash_func page_hash;
hash_less_func page_less;

void page_exit (void);
bool page_in (void *fault_addr);
bool page_out (struct page *p);
bool page_accessed_recently (struct page *p);
struct page * page_allocate (void *vaddr, bool read_only);
void page_deallocate (void *vaddr);
unsigned page_hash (const struct hash_elem *e, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);
bool page_lock (const void *addr, bool will_write);
void page_unlock (const void *addr);

#endif
