#ifndef FRAME_H
#define FRAME_H

#include <stdbool.h>
#include "threads/synch.h"
#include "vm/page.h"

struct frame {
  struct lock lock;
  void *base;
  struct page *page;
};

typedef struct frame frame;

//Initialize the frame table
void frame_init (void);

// Evict a frame, update pagedir, frame table, send data
// to whereever and update stuff
void frame_evict (struct page *p);
//Get a frame from the frame table
//Returns null if no empty frames 
bool obtain_frame(struct page *p);

//Frees a frame
//Returns true if frame was freed
bool free_frame(frame *f);

void acquire_scan_lock();
void release_scan_lock();


#endif
