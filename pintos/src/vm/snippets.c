
void 
frame_table_init(void)
{
  lock_init(&frame_table_lock);
  hash_init(&frame_table, thread_hash, thread_less, NULL);
}

/* actually allocates and installs a user page.  NOT LAZY */
void *
install_user_page(struct thread *t, void *upage, enum palloc_flags flags UNUSED)
{
  frame_table_entry *fe = (frame_table_entry *)malloc(sizeof(frame_table_entry));
  void *frame = palloc_get_page(PAL_USER | PAL_ZERO);
  
  if (frame == NULL)
  {
    return NULL;
  }

  fe->tid = t->tid;
  fe->frame = frame;
  fe->upage = upage; 
  fe->on_swap = false;
  fe->zeroes = false;

  lock_acquire(&frame_table_lock);
  t->stack_pages += 1;
  hash_insert(&frame_table, &fe->elem);
  bool result = set_page(t, upage, frame);
  lock_release(&frame_table_lock);
  
  if (!result)  
  {
    palloc_free_page(frame);
    return NULL;
  }

  return frame;
}

/* lazy load a page from t->exec_lock file at pos and read bytes size*/
void
lazy_install_user_page(struct thread *t, 
                       void *upage, 
                       int32_t pos, 
                       uint32_t size,
                       bool zeroes)
{
  frame_table_entry *fe = (frame_table_entry *)malloc(sizeof(frame_table_entry));
  fe->tid = t->tid;
  fe->upage = upage;
  fe->pos = pos;
  fe->size = size;
  fe->zeroes = zeroes;
  fe->on_swap = false;
  
  lock_acquire(&frame_table_lock);
  hash_insert(&frame_table, &fe->elem);
  lock_release(&frame_table_lock);
}

//TODO - NEED TO MOVE EXEC_LOCK WHEN YOU MODIFY PROCESS FOR LAZY LOADING


/* Called from page fault to actually load a page that was
 * setup via lasy_install_user_page
 */
void *
load_lazy_page(struct thread *t, void *upage)
{
  frame_table_entry *entry = find_closest(t, upage);
  if (entry != NULL)
  {
    if (entry->zeroes)
    {
       void *frame = palloc_get_page(PAL_USER | PAL_ZERO);
       if (frame == NULL)
       {
         return NULL;
       }
       
       bool result = set_page(t, upage, frame);
       if (!result)
       {
         palloc_free_page(frame);
         return NULL;
       }
       return frame;
    }
    else
    {
      void *frame = palloc_get_page(PAL_USER | PAL_ZERO);
      if (frame == NULL)
      {
        return NULL;
      }

      struct file *exec_file = t->exec_lock;    
      lock_acquire(&frame_table_lock);
      file_seek(exec_file, entry->pos);
      file_read(exec_file, frame, (off_t)entry->size);
      lock_release(&frame_table_lock);
      
      bool result = set_page(t, upage, frame);
      if (!result)
      {
        palloc_free_page(frame);
        return NULL;
      }
      return frame;
    }
  }
  else 
  {
    return NULL;
  }
}

//finds the page that contains access of thread t
//returns null if no such page
static frame_table_entry*
find_closest(struct thread *t, void *access)
{
  struct list *bucket = find_bucket_by_index(&frame_table, hash_int(t->tid));
  if (bucket != NULL)
  {
    struct list_elem *i;
    i = list_begin(bucket);
    while (i != list_end(bucket))
    {
      struct hash_elem *hi = list_elem_to_hash_elem (i);
      frame_table_entry *entry = hash_entry(hi, frame_table_entry, elem);
      if (entry->upage <= access && access <= (entry->upage + PGSIZE))
      {
        return entry;
      }
    }
  }
  return NULL;
}

//Private method to simplify adding a page to a page table
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


bool
is_on_stack(void *access, void *esp) 
{
  //printf("%x | %x | %x \n", (int)PHYS_BASE, (int)access, (int)esp);
  if (access < (PHYS_BASE - 4) && (((uint32_t)esp - 32) <= (uint32_t)access) && esp <= PHYS_BASE) 
  {
    //printf("is_on_stack(true)\n");
    return true;
  }
  return false;
}

/* TODO - TEST/DEBUG THIS */
//TODO - THIS also needs to clear swap

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

