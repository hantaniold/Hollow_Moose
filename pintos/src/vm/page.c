#include "vm/page.h"
#include <hash.h>
#include <stdbool.h>
#include "threads/thread.h"
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

//Hash tables functions

unsigned
page_hash(const struct hash_elem *e, void *aux UNUSED) 
{
  page *p = hash_entry(e, page, hash_elem);
  return hash_int((int)p->addr);
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
  return NULL;    
}

static bool do_page_in (struct page *p) 
{
  return false;
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

bool page_in (void *fault_addr) 
{
  return false;  
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
  }
  else
  {
    return NULL;
  }
  
  return p;
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
  return false; 
}


