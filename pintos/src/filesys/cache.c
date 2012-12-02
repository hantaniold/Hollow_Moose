#include "filesys/cache.h"
#include <debug.h>
#include <string.h>
#include "filesys/filesys.h"
#include "devices/timer.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define INVALID_SECTOR ((block_sector_t) -1)

struct cache_block 
  {
    struct lock block_lock;
    struct condition no_readers_or_writers;
    struct condition no_writers;           
    int readers, read_waiters;
    int writers, write_waiters;
    block_sector_t sector;
    bool up_to_date;
    bool dirty;
    struct lock data_lock; 
    uint8_t data[BLOCK_SECTOR_SIZE];   
  };

/* Cache. */
#define CACHE_CNT 64
struct cache_block cache[CACHE_CNT];
struct lock cache_sync;
static uint32_t hand = 0;


static void flushd_init (void);
static void readaheadd_init (void);
static void readaheadd_submit (block_sector_t sector);

static struct cache_block *in_cache(block_sector_t sector);
static struct cache_block *find_empty();
static struct cache_block *try_to_empty();
static void flush_block(struct cache_block *cb);
static void clear_block(struct cache_block *cb);

/* Initializes cache. */
void
cache_init (void) 
{
  lock_init(&cache_sync);
  int i;
  for (i = 0; i < CACHE_CNT; ++i) {
    struct cache_block *cb = &cache[i];
    lock_init(&cb->block_lock);
    lock_init(&cb->data_lock);
    cond_init(&cb->no_readers_or_writers);
    cond_init(&cb->no_writers);
    clear_block(cb);
  }
  flushd_init();
}

//cache_sync must already be acquired
struct cache_block * 
in_cache(block_sector_t sector)
{
  int i;
  for (i = 0; i < CACHE_CNT; ++i){
   struct cache_block *cb = &cache[i];
   if (cb->sector == sector) {
     return cb;
   }
  }
  return NULL;
}

//cache_sync must already be acquired
struct cache_block *
find_empty()
{
  int i;
  for (i = 0; i < CACHE_CNT; ++i){
   struct cache_block *cb = &cache[i];
   if (cb->sector == -1) {
     return cb;
   }
  }
  return NULL; 
}

//cache_sync must already be acquired
//TODO - NEED TO ACCOUNT FOR value of metadata versus data
struct cache_block *
try_to_empty()
{
  uint32_t hand_start = hand;
   
  for (; hand < CACHE_CNT; ++hand){
   struct cache_block *cb = &cache[hand];
   if ( cb->readers == 0 &&
        cb->read_waiters == 0 &&
        cb->writers == 0 &&
        cb->write_waiters == 0) {
    flush_block(cb); 
    return cb;
   }
  }
  hand = 0;
  for (; hand < hand_start; ++hand) {
    struct cache_block *cb = &cache[hand];
    if ( cb->readers == 0 &&
        cb->read_waiters == 0 &&
        cb->writers == 0 &&
        cb->write_waiters == 0) {
      flush_block(cb); 
      return cb;
    }
  }
  return NULL;  
}

void 
flush_block(struct cache_block *cb)
{
  lock_acquire(&cb->data_lock);
  if (cb->dirty && cb->sector > INVALID_SECTOR)
  {
    block_write(fs_device, cb->sector, cb->data);
  }
  lock_release(&cb->data_lock);
}


void 
clear_block(struct cache_block *cb)
{
  cb->readers = 0;
  cb->read_waiters = 0;
  cb->writers = 0;
  cb->write_waiters = 0;
  cb->sector = -1;
  cb->up_to_date = true;
  cb->dirty = false;
}

/* Flushes cache to disk. */
void
cache_flush (void) 
{
  lock_acquire(&cache_sync);
  int i;
  for (i = 0; i < CACHE_CNT; ++i) {
    struct cache_block *cb = &cache[i];
    flush_block(cb);   
  }
  lock_release(&cache_sync);
}

/* Locks the given SECTOR into the cache and returns the cache
   block.
   If TYPE is EXCLUSIVE, then the block returned will be locked
   only by the caller.  The calling thread must not already
   have any lock on the block.
   If TYPE is NON_EXCLUSIVE, then block returned may be locked by
   any number of other callers.  The calling thread may already
   have any number of non-exclusive locks on the block. */
struct cache_block *
cache_lock (block_sector_t sector, enum lock_type type) 
{
  int i;

  try_again:
  lock_acquire(&cache_sync);
  /* Is the block already in-cache? */
  struct cache_block *cb;
  cb = in_cache(sector);
  if (cb != NULL) {
    //TODO - LOCKS AND SUCH
    lock_release(&cache_sync); 
    return cb;
  } else {
    /* Not in cache.  Find empty slot. */
    cb = find_empty(); 
    if (cb != NULL) {
      //TODO - LOCKS AND SUCH
      lock_release(&cache_sync);
      return cb;
    } else {
      /* No empty slots.  Evict something. */
      cb = try_to_empty();
      if (cb != NULL) {
        //TODO - LOCKS AND SUCH
        lock_release(&cache_sync);
        return cb;
      } else {
        /* Wait for cache contention to die down. */

        // sometimes, you might get into a situation where you
        // cannot find a block to evict, or you cannot lock
        // the desired block. If that's the case there might
        // some contention. So the safest way to do this, is to
        // release the cache_sync lock, and sleep for 1 sec, and
        // try again the whole operation.

        lock_release (&cache_sync);
        timer_msleep (1000);
        goto try_again;
      } //try_to_empty 
    } //find_empty
  } //in_cache
} //end cache_lock

/* Bring block B up-to-date, by reading it from disk if
   necessary, and return a pointer to its data.
   The caller must have an exclusive or non-exclusive lock on
   B. */
void *
cache_read (struct cache_block *b) 
{
  // ...
  //      block_read (fs_device, b->sector, b->data);
  // ...
}

/* Zero out block B, without reading it from disk, and return a
   pointer to the zeroed data.
   The caller must have an exclusive lock on B. */
void *
cache_zero (struct cache_block *b) 
{
  // ...
  //  memset (b->data, 0, BLOCK_SECTOR_SIZE);
  // ...

}

/* Marks block B as dirty, so that it will be written back to
   disk before eviction.
   The caller must have a read or write lock on B,
   and B must be up-to-date. */
void
cache_dirty (struct cache_block *b) 
{
  // ...
}

/* Unlocks block B.
   If B is no longer locked by any thread, then it becomes a
   candidate for immediate eviction. */
void
cache_unlock (struct cache_block *b) 
{
  // ...
}

/* If SECTOR is in the cache, evicts it immediately without
   writing it back to disk (even if dirty).
   The block must be entirely unused. */
void
cache_free (block_sector_t sector) 
{
  // ...
}


/* Flush daemon. */

static void flushd (void *aux);

/* Initializes flush daemon. */
static void
flushd_init (void) 
{
  thread_create ("flushd", PRI_MIN, flushd, NULL);
}

/* Flush daemon thread. */
static void
flushd (void *aux UNUSED) 
{
  for (;;) 
    {
      timer_msleep (30 * 1000);
      cache_flush ();
    }
}
