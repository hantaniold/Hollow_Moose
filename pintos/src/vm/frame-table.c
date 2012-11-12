#include <list.h>
#include <hash.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
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
void clear_frame_table(struct thread *t);
void *install_user_page(struct thread *t, void *upage, enum palloc_flags flags);

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
  frame_table_entry *fe = (frame_table_entry *)malloc(sizeof(frame_table_entry));
  void *frame = palloc_get_page(PAL_USER | flags);
  
  fe->tid = t->tid;
  fe->frame = frame;
  fe->upage = upage; 
  lock_acquire(&frame_table_lock);
  hash_insert(&frame_table, &fe->elem);
  pagedir_get_page (t->pagedir, upage);
  pagedir_set_page (t->pagedir, upage, frame, true);
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

void
clear_frame_table(struct thread *t)
{
  struct list *bucket = find_bucket_by_index(&frame_table, hash_int(t->tid));
  if (bucket != NULL)
  {
    struct list_elem *i;
    i = list_begin(bucket);
    lock_acquire(&frame_table_lock);
    while (i != list_end(bucket))
    {
      struct hash_elem *hi = list_elem_to_hash_elem (i);
      frame_table_entry *entry = hash_entry(hi, frame_table_entry, elem);
      if (entry->tid == t->tid)
      {
        i = list_remove(i);
        pagedir_clear_page(t->pagedir, entry->upage); 
        free(entry);
      }
      else
      {
        i = list_next(i);
      }
    }
    lock_release(&frame_table_lock);
  }
}

