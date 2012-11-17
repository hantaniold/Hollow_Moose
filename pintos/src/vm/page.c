#include "vm/page.h"
#include <hash.h>
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
  return;     
}

static struct page *page_for_addr (const void *address) 
{
  struct thread *t = thread_current();
  
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
  struct file *f = p->thread->exec_lock;

  uint32_t read_bytes = p->read_bytes;
  uint32_t zero_bytes = p->zero_bytes;
  
  if (f != NULL && p->frame != NULL)
  {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs (p->addr) == 0);
    ASSERT(p->ofs % PGSIZE == 0);

    file_seek(f, p->ofs);
    while (read_bytes > 0 || zero_bytes > 0)
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      
      uint8_t *kaddr = (uint8_t *)p->frame->base;
      if (file_read (f, kaddr, page_read_bytes) != (int) page_read_bytes)
      {
        return false;
      }
      memset(kaddr + page_read_bytes, 0, page_zero_bytes);
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
    }
    return true;
  }
  else
  {
    return false;
  }
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

//??? What is this for
void page_exit (void)  
{
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
      PANIC ("SOMETHING EQUAL IN TABLE PANIC!!!\n");
    }
    
    return p;
    /*
    bool of = obtain_frame(p);
    if (of)
    {
      p->addr = vaddr;
      p->read_only = read_only;
      p->thread = t; 
      hash_insert(t->pages, &p->hash_elem);
      if (set_page(t, vaddr, p->frame->base))
      {
        return p;
      }
      else
      {
        printf("SET_PAGE FAILED\n");
        //TODO - deallocate frame
        free(p);
        return NULL;
      }
    }
    else
    {
      free(p);
      return NULL;
    }
    */
  }
  else
  {
    return NULL;
  }
}

void page_deallocate (void *vaddr) 
{
 return;   
}

bool page_lock (const void *addr, bool will_write) 
{
  return false;  
}
void page_unlock (const void *addr) 
{
  return; 
}


