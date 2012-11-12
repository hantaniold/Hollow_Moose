#ifndef FRAME_TABLE_H
#define FRAME_TABLE_H

#include <debug.h>
#include <hash.h>
#include <stdint.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"

typedef struct {
  tid_t tid;
  void * frame;      
  void * upage;
  struct hash_elem elem;
  
} frame_table_entry;

//Need to call before using frame table.
//Called in thread.c when kernel starts
void frame_table_init(void);

//Gets a user page. Adds the page to the frame table.
void *get_user_page(struct thread *t, void *upage, enum palloc_flags flags);
 
//Returns true if the pointer is likely a stack reference.
//false otherwise
bool is_on_stack(void *access, void *esp); 

void clear_frame_table(struct thread *t);

#endif
