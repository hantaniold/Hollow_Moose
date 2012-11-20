#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"
#include <stdio.h>

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
  hand = 0;
  
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

// Pick a frame to evict, kick it out of its house,
// send its children to adoption agency
void
frame_evict (page *p)
{
  lock_acquire (&scan_lock);
  
  // Clock algorithm
  frame * f;
  struct page * iter_p;
  size_t init_hand = hand;
  do 
  {
    // Increment the hand, grab the next frame.
    (++hand >= frame_count) ? hand = 0 : 1 ;
    f = &frames[hand];

    // If this page hasn't been accessed then break out
    // of this do-while loop.
    lock_acquire(&f->lock);
    iter_p = f->page;
    lock_release (&f->lock);
    if (false == pagedir_is_accessed(iter_p->thread->pagedir,iter_p->addr)) 
    {
      hand = init_hand;
    }

    // In every case we want to set the access bit to false
    pagedir_set_accessed(iter_p->thread->pagedir,iter_p->addr,false);

  } while (init_hand != hand);

  // Victim picked, now acqurie its lock
  lock_acquire(&f->lock);

  struct thread * t = thread_current ();
  struct pagedir * pd = (struct pagedir *) t->pagedir;
  struct page * p_evicted = f->page;
  p_evicted->frame = NULL;
  p_evicted->in_memory = false;

  f->page = NULL;

  // Write to swap or back to its file?
  swap_out(p_evicted);

  // update that metadata in the page's owner
  pagedir_clear_page((uint32_t *) pd, p->addr);

  //hand the frame to its new owner
  p->frame = f;
  f->page = p;

  lock_release (&f->lock);
  lock_release (&scan_lock);

}

bool 
obtain_frame(page *p) 
{
  //printf("USED_FRAMES %d\n", used_frames);
  if (used_frames < frame_count)
  {
    lock_acquire(&scan_lock);
    unsigned i = 0;
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
  //PANIC ("OUT OF MEMORY\n");
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
