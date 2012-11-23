#include <hash.h>
#include "vm/page.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/pagedir.h"
#include "devices/block.h"

#define STACK_MAX (1024 * 1024)

static void destroy_page (struct hash_elem *p_, void *aux);
static struct page *page_for_addr (const void *address);
static bool do_page_in (struct page *p);
static bool set_page(struct thread *t, void * upage, void *frame, bool writable);
static bool load_from_exec(page *p);

static void page_do_evict (page *p);
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
      if (!p->in_memory && p->sector != -1)
      {
        //printf("BEFORE SWAP READ with addr %x\n", p->addr);
        swap_read(p);
        //printf("AFTER SWAP READ\n");
        p->in_memory = true;
        p->sector == -1;
      }
      if (p->file != NULL && p->file_offset >= 0 && p->file_bytes >= 0)
      {
        file_seek(p->file, p->file_offset);
        file_write(p->file,(char *)p->frame->base, p->file_bytes); 
      }
      free_frame(p->frame); 
    }
      
    pagedir_clear_page(p->thread->pagedir, p->addr);

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
    
    //printf("addr_cmp %x , vaddr_cmp %x\ , address %x\n", addr_cmp, vaddr_cmp, address);


    if (addr_cmp >= vaddr_cmp && addr_cmp < (vaddr_cmp + PGSIZE))
    {
      return p;
    }
  }
  return NULL;
}

static bool do_page_in (struct page *p) 
{
  bool writable = true;
  if (!p->in_memory && p->sector != -1)
  {
    //printf("BEFORE SWAP READ with addr %x\n", p->addr);
    swap_read(p);
    //printf("AFTER SWAP READ\n");
    p->in_memory = true;
    p->sector == -1;
  }
  else if (p->from_exec)
  {
    bool b = load_from_exec(p);
    writable = true;
    if (!b)
    {
      printf("LOAD FROM FILE FAILED\n");
      return false;
    }
  }
  else if (p->mmap)
  {
    bool b = load_from_file(p);
    if (!b)
    {
      return false;
    }
  }
  else {}
  struct thread *t = thread_current();
  if (set_page(t, p->addr, p->frame->base, writable))
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
    //printf("READ_BYTES: %d ZERO_BYTES %d ADDR %d\n", read_bytes, zero_bytes, (uint32_t)p->addr);
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs (p->addr) == 0);
    ASSERT(p->ofs % PGSIZE == 0);

    file_seek(f, p->ofs);
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
set_page(struct thread *t, void * upage, void *frame, bool writable)
{
  if (pagedir_get_page (t->pagedir, upage) == NULL)
  {
    return pagedir_set_page (t->pagedir, upage, frame, writable);
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
  acquire_scan_lock();
  struct thread *t = thread_current();
  hash_destroy (&t->pages, destroy_page);
  release_scan_lock();
  return;
}

//TODO - finish this
bool page_in (void *fault_addr) 
{
  //printf("PAGE_In FAULT ADDR %x\n", (uint32_t)fault_addr);
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
      if (of && p->frame != NULL)
      {
        lock_acquire(&p->frame->lock);
        
        bool b = do_page_in (p);
        if (b)
        {
          p->in_memory = true;
          lock_release(&p->frame->lock);
          return true;
        }
        else
        {
          p->in_memory = false;
          lock_release(&p->frame->lock);
          return false;
        }
      }
      else
      {
        //printf("EVICTING PAGE\n");
        page_do_evict (p);
       
        lock_acquire(&p->frame->lock);

        bool b = do_page_in (p);
        if (b)
        {
          p->in_memory = true;
          lock_release(&p->frame->lock);
          //printf("EXIT evict true\n");
          return true;
        }
        else
        {
          p->in_memory = false;
          //printf("EXIT evict false\n");
          lock_release(&p->frame->lock);
          return false;
        }
      }
       
    }
    return true;
  }
  else
  {
    return false;
  }
}

static void 
page_do_evict (page *p) 
{
  // We use "FNO" algorithm
  frame_evict (p); 
}

bool page_out (struct page *p) 
{
  p = p;
  return false;
}

bool page_accessed_recently (struct page *p) 
{
  p = p;
  return false;  
}

struct page * page_allocate (void *vaddr, bool read_only) 
{
  struct thread *t = thread_current(); 
  
  page *p = (page *)malloc(sizeof(page));
  if (p != NULL)
  {
    p->frame = NULL;
    p->addr = vaddr;
    p->read_only = read_only;
    p->thread = t;
    p->in_memory = false;
    p->from_exec = false;
    p->mmap = false;
    p->sector = -1;
    p->on_stack = false;

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
    struct thread *t = thread_current();
    if (p->frame != NULL)
    {
      if (p->file != NULL && p->file_offset >= 0 && p->file_bytes >= 0)
      {
       if (pagedir_is_dirty(t->pagedir, p->addr))
       {
         file_seek(p->file, p->file_offset);
         file_write(p->file,(char *)p->frame->base, p->file_bytes); 
        }
      }
      free_frame(p->frame);
    }
      
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
  addr = addr;
  will_write = will_write;
  return false;  
}
void page_unlock (const void *addr) 
{
  addr = addr;
  return; 
}


