#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include "vm/page.h"


static frame *frames;
static size_t frame_count;
static size_t used_frames;
//used to allocate empty frames

static struct lock scan_lock;
static size_t hand;

void
frame_init(void)
{
  void *base;
  
  lock_init(&scan_lock);
  
  frame_count = 0;
  used_frames = 0;
  //init_ram_pages
  //Should be using init_ram_pages, but ghetto turned up
  //to 9001 to make things work for now
  frames = malloc (sizeof(*frames) * init_ram_pages);

  if (frames == NULL)
  {
    PANIC ("out of memory allocating page frames");
  }
  
  //&& frame_count < 20
  while ((base = palloc_get_page (PAL_USER)) != NULL)
  {
    frame *f = &frames[frame_count++];
    lock_init(&f->lock);
    f->base = base;
    f->page = NULL;
  }
}

bool 
obtain_frame(page *p) 
{
  //printf("USED_FRAMES %d\n", used_frames);
  if (used_frames < frame_count)
  {
    lock_acquire(&scan_lock);
    int i = 0;
    //printf("FRAME COUNT: %d\n", frame_count);
    while (i < frame_count)
    {
      frame *f = &frames[i];
      if (f->page == NULL)
      {
        lock_acquire(&f->lock);
        f->page = p;
        p->frame = f;
        used_frames++;
        lock_release(&f->lock);
        lock_release(&scan_lock);
        return true;
      }
      i++;
    }
    lock_release(&scan_lock);
    return false;
  }
  //TODO - Once swapping works, we'll remove this panic
  PANIC ("OUT OF MEMORY\n");
  return false;
}

bool
free_frame(frame *f)
{
  if (f != NULL)
  {
    lock_acquire(&f->lock);
    f->page = NULL;
    lock_release(&f->lock);
    return true;
  }
  return false;
}
