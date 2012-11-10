#include <hash.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "vm/frame-table.h"

//FRAME TABLE GLOBALS
static struct hash frame_table;
static struct lock frame_table_lock;

//FORWARD DEFS
unsigned thread_hash(const struct hash_elem *e, void *aux); 
bool thread_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);
static bool install_page (struct thread *t, void *upage, void *kpage, bool writable);
void * install_user_page(struct thread *t, void *upage, enum palloc_flags flags);


unsigned
thread_hash(const struct hash_elem *e, void *aux UNUSED) 
{
  frame_table_entry *fe = hash_entry(e, frame_table_entry, elem);
  return hash_int(fe->tid);
}

bool
thread_less(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
  unsigned hash_a = thread_hash(a, aux);
  unsigned hash_b = thread_hash(b, aux);

  if (hash_a < hash_b) 
  {
    return true;
  } 
  else if (hash_b < hash_a) 
  {
    return false;
  } 
  else 
  {
    frame_table_entry *fe_a = hash_entry(a, frame_table_entry, elem);
    frame_table_entry *fe_b = hash_entry(b, frame_table_entry, elem);
    if (fe_a->frame < fe_b->frame) 
    {
      return true;
    } else 
    {
      return false;
    }
  }
}

void 
frame_table_init(void)
{
  lock_init(&frame_table_lock);
  hash_init(&frame_table, thread_hash, thread_less, NULL);
}

void *
install_user_page(struct thread *t, void *upage, enum palloc_flags flags)
{
  //TODO - IT IS INSANE TO USE ONE PAGE FOR FRAME METADATA u
  //AND ANOTHER FOR THE PAGE. FIX THIS!!!
  frame_table_entry *fe = (frame_table_entry *)palloc_get_page(PAL_ZERO);
  void *frame = palloc_get_page(PAL_USER | flags);
  
  fe->tid = t->tid;
  fe->frame = frame;
  
  lock_acquire(&frame_table_lock);
  hash_insert(&frame_table, &fe->elem);
  //always writeable for now, probably should change this
  install_page(t, upage, frame, true);
  lock_release(&frame_table_lock);

  return frame;
}

bool
is_on_stack(void *access, void *esp) 
{
  if (access < PHYS_BASE && esp > access) 
  {
    return true;
  }
  return false;
}

//TAKEN FROM PROCESS.C
/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (struct thread *t, void *upage, void *kpage, bool writable)
{
  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

