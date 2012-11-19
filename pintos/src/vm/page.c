#include <hash.h>
#include "vm/page.h"
#include <stdbool.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/pagedir.h"
#include "devices/block.h"

#define STACK_MAX (1024 * 1024)

static void destroy_page (struct hash_elem *p_, void *aux);
static struct page *page_for_addr (const void *address);
static bool do_page_in (struct page *p);
static bool set_page(struct thread *t, void * upage, void *frame);
static bool load_from_exec(page *p);

static void page_do_evict (void);
static bool load_from_file(page *p);

//Hash tables functions

unsigned
page_hash(const struct hash_elem *e, void *aux UNUSED) 
{
  page *p = hash_entry(e, page, hash_elem);
  unsigned o = hash_int((int)p->addr);
  return o;
}

bool
page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  page *p1 = hash_entry(a, page, hash_elem);
  page *p2 = hash_entry(b, page, hash_elem);
  if (p1->addr < p2->addr) 
  {
    return true;
  } else 
  {
    return false;
  }
}

//Static functions

static void 
destroy_page (struct hash_elem *p_, void *aux UNUSED)  
{
  page *p = hash_entry(p_, page, hash_elem);

  if (p != NULL)
  {
    if (p->frame != NULL)
    {
      if (p->file != NULL && p->file_offset >= 0 && p->file_bytes >= 0)
      {
        file_seek(p->file, p->file_offset);
        file_write(p->file,(char *)p->frame->base, p->file_bytes); 
      }
      free_frame(p->frame);
    }
      
    struct thread *t = thread_current();
    pagedir_clear_page(t->pagedir, p->addr);

    free(p);
  }
  return;
}

static struct page *
page_for_addr (const void *address) 
{
  struct thread *t = thread_current();
 
  //printf("TID: %d\n", t->tid);

  struct hash_iterator i;
  hash_first(&i, &t->pages);
  
  while (hash_next(&i))
  {
    page *p = hash_entry(hash_cur(&i), page, hash_elem);
    
    uint8_t *addr_cmp = (uint8_t *)address;
    uint8_t *vaddr_cmp = (uint8_t *)p->addr;
    
    //printf("addr_cmp %x , vaddr_cmp %x\m , address %x\n", addr_cmp, vaddr_cmp, address);


    if (addr_cmp >= vaddr_cmp && addr_cmp <= (vaddr_cmp + PGSIZE))
    {
      return p;
    }
  }
  return NULL;
}

static bool do_page_in (struct page *p) 
{
  return false;
}

static bool 
load_from_exec(page *p)
{
  //printf("LOADING FROM EXEC\n");
  struct file *f = p->thread->exec_lock;

  uint32_t read_bytes = p->read_bytes;
  uint32_t zero_bytes = p->zero_bytes;
  
  if (f != NULL && p->frame != NULL)
  {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs (p->addr) == 0);
    ASSERT(p->ofs % PGSIZE == 0);

    file_seek(f, p->ofs);
    lock_acquire(&p->frame->lock);
    void *base = p->frame->base;
    
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    
    if (file_read (f, base, page_read_bytes) != (int) page_read_bytes)
    {
      return false;
    }

    memset(base + page_read_bytes, 0, page_zero_bytes);
    //read_bytes -= page_read_bytes;
    //zero_bytes -= page_zero_bytes;
   
    lock_release(&p->frame->lock);
    return true;
  }
  else
  {
    return false;
  }
}

static bool
load_from_file(page *p)
{
  struct file *fp = p->file;
  file_seek(fp, p->file_offset);
  file_read(fp, p->frame->base, p->file_bytes);
  if (p->file_bytes < PGSIZE)
  {
    uint32_t zero_count = PGSIZE - p->file_bytes;
    memset(p->frame->base + p->file_bytes, 0, zero_count);
  }
  return true;
}


static bool
set_page(struct thread *t, void * upage, void *frame)
{
  if (pagedir_get_page (t->pagedir, upage) == NULL)
  {
    return pagedir_set_page (t->pagedir, upage, frame, true);
  }
  else
  {
    return false;
  }
}

//global functions

//Called to free memory of a page upon process exit
void page_exit (void)  
{
  struct thread *t = thread_current();
  hash_destroy (&t->pages, destroy_page);
  return;
}

//TODO - finish this
bool page_in (void *fault_addr) 
{
  page *p = page_for_addr(fault_addr);
  if (p != NULL)
  {
    if (p->frame != NULL)
    {
      return true;
    }
    else
    { 
      bool of = obtain_frame(p);
      if (of)
      {
        if (p->from_exec)
        {
          bool b = load_from_exec(p);
          if (!b)
          {
            printf("LOAD FROM FILE FAILED\n");
            return false;
          }
        }
        if (p->mmap)
        {
         load_from_file(p); 
        }
        struct thread *t = thread_current();
        if (set_page(t, p->addr, p->frame->base))
        {
          return true;
        }
        else
        {
          printf("SET_PAGE FAILED\n");
          //TODO - deallocate frame
          free(p);
          return false;
        }
      }
      else
      {
        // Couldn't obtain frame, need to evict.
        page_do_evict ();
        // Load in the frame via mmap or from swap (if either)
        //   -update pagedir
        //   -update frame table
        //   -update pages in thread
        printf("OBTAIN FRAME FAILURE\n");
        return false;
      } 
    }
    return true;
  }
  else
  {
    printf("P WAS NULL\n");
    return false;
  }
}

static void 
page_do_evict (void) 
{
  // We use "FNO" algorithm

  frame_evict (); 

  // Update frame table info, page dir mapping
  // If mmap'd...
  // otherwise swap to disk
}

bool page_out (struct page *p) 
{
  return false;
}

bool page_accessed_recently (struct page *p) 
{
  return false;  
}

struct page * page_allocate (void *vaddr, bool read_only) 
{
  struct thread *t = thread_current(); 
  
  page *p = (page *)malloc(sizeof(page));
  if (p != NULL)
  {
    p->addr = vaddr;
    p->read_only = read_only;
    p->thread = t;

    struct hash_elem *h = hash_insert(&t->pages, &p->hash_elem);
    if (h == NULL)
    {
      return p;
    } 
    else
    {
      return NULL;
      //PANIC ("SOMETHING EQUAL IN TABLE PANIC!!!\n");
    }
    
    return p;
  }
  else
  {
    return NULL;
  }
}

void 
page_deallocate (void *vaddr) 
{
  page * p = page_for_addr (vaddr);

  if (p != NULL)
  {
   
    if (p->frame != NULL)
    {
      if (p->file != NULL && p->file_offset >= 0 && p->file_bytes >= 0)
      {
        file_seek(p->file, p->file_offset);
        file_write(p->file,(char *)p->frame->base, p->file_bytes); 
      }
      free_frame(p->frame);
    }
      
    struct thread *t = thread_current();
    pagedir_clear_page(t->pagedir, p->addr);
   
    hash_delete(&t->pages, &p->hash_elem);

    free(p);
  }
}

page *
page_by_addr(void *vaddr)
{
  return page_for_addr (vaddr);
}


bool page_lock (const void *addr, bool will_write) 
{
  return false;  
}
void page_unlock (const void *addr) 
{
  return; 
}


