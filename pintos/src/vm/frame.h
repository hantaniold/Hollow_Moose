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

//Get a frame from the frame table
//Returns null if no empty frames 
bool obtain_frame(struct page *p);

#endif
