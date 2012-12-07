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

static bool DEBUG = true;

//TODO - VERIFY THAT OUR TIMER WORKS AND POTENTIALLY BRING IN PROJECT 1 CODE
static void flushd_init (void);

//TODO - IMPLEMENT THESE
static void readaheadd_init (void);
static void readaheadd_submit (block_sector_t sector);

static struct cache_block *in_cache(block_sector_t sector);
static struct cache_block *find_empty(void);
static struct cache_block *try_to_empty(void);
static void flush_block(struct cache_block *cb);
static void clear_block(struct cache_block *cb);
static bool add_exclusive_lock(struct cache_block *cb);
static bool add_nonexclusive_lock(struct cache_block *cb);
static bool add_lock(struct cache_block *cb, enum lock_type t);

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
    cb->readers = 0;
    cb->read_waiters = 0;
    cb->writers = 0;
    cb->write_waiters = 0;
    clear_block(cb);
  }
  flushd_init();
}

//cache_sync must already be acquired
struct cache_block * 
in_cache(block_sector_t sector)
{
  if (DEBUG) {
    printf("in_cache looking for sector %d\n", sector);
  }
  uint8_t i;
  for (i = 0; i < CACHE_CNT; ++i){
    struct cache_block *cb = &cache[i];
    if (!lock_held_by_current_thread(&cb->block_lock) && 
         lock_try_acquire(&cb->block_lock)) {
      if (cb->sector == sector) {
        if (DEBUG) {
          printf("in_cache returning %d\n", i);
        }
        return cb;
      }
      lock_release(&cb->block_lock); 
    }
  }
  return NULL;
}

//cache_sync must already be acquired
struct cache_block *
find_empty(void)
{
  uint8_t i;
  for (i = 0; i < CACHE_CNT; ++i){
    struct cache_block *cb = &cache[i];
    if (!lock_held_by_current_thread(&cb->block_lock) && 
         lock_try_acquire(&cb->block_lock)) {
      if (cb->sector == INVALID_SECTOR) {
        if (DEBUG) {
          printf("find_empty returning %d\n", i);
        }
        return cb;
      }
      lock_release(&cb->block_lock); 
    }
  }
  return NULL; 
}

//cache_sync must already be acquired
//TODO - NEED TO MAKE THIS TRY ACQUIRE CODE
//TODO - NEED TO ACCOUNT FOR value of metadata versus data
struct cache_block *
try_to_empty(void)
{
  if (DEBUG) {
    printf("ENTER TRY TO EMPTY\n");
  }
  uint32_t hand_start = hand;
   
  for (; hand < CACHE_CNT; ++hand){
    struct cache_block *cb = &cache[hand];
    if (!lock_held_by_current_thread(&cb->block_lock) && 
         lock_try_acquire(&cb->block_lock))
    {
      if (DEBUG) {
        printf("ENTER 1 - %d\n", hand);
        printf("r: %d rw: %d w: %d ww: %d\n", 
                cb->readers, 
                cb->read_waiters, 
                cb->writers, 
                cb->write_waiters);
      }
      if ( cb->readers == 0 &&
           cb->read_waiters == 0 &&
           cb->writers == 0 &&
           cb->write_waiters == 0) {
        flush_block(cb); 
        return cb;
      }
      lock_release(&cb->block_lock);
    }
  }
  hand = 0;
  for (; hand < hand_start; ++hand) {
    struct cache_block *cb = &cache[hand];
    if (!lock_held_by_current_thread(&cb->block_lock) && 
         lock_try_acquire(&cb->block_lock))
    {
      if (DEBUG) {
        printf("ENTER 2 - %d\n", hand);
      }
      if ( cb->readers == 0 &&
           cb->read_waiters == 0 &&
           cb->writers == 0 &&
           cb->write_waiters == 0) {
        flush_block(cb); 
        return cb;
      }
      lock_release(&cb->block_lock);
    }

  }
  return NULL;  
}

//cache_sync must already be held
//cb->block_lock must already be held
bool 
add_lock(struct cache_block *cb, enum lock_type t)
{
  if (t == EXCLUSIVE) {
    return add_exclusive_lock(cb);
  } else {
    return add_nonexclusive_lock(cb);
  }
}


//cache_sync must already be acquired
//exclusive block_locks are not released
bool 
add_exclusive_lock(struct cache_block *cb)
{
  cb->writers += 1;
  return true;
}

//cache_sync must already be acquired
//non_exclusive will release block_lock
bool 
add_nonexclusive_lock(struct cache_block *cb)
{
  cb->readers += 1;
  lock_release(&cb->block_lock);
  return true;
}

//cache_sync must already be acquired
void 
flush_block(struct cache_block *cb)
{
  lock_acquire(&cb->data_lock);
  if (cb->dirty && cb->sector != INVALID_SECTOR)
  {
    if (DEBUG) {
      printf("cache_flush flushing sector %d\n", cb->sector);
    }
    block_write(fs_device, cb->sector, cb->data);
    clear_block(cb);
  }
  lock_release(&cb->data_lock);
}


void 
clear_block(struct cache_block *cb)
{
  cb->sector = INVALID_SECTOR;
  cb->up_to_date = false;
  cb->dirty = false;
}

/* Flushes cache to disk. */
void
cache_flush (void) 
{
  if (DEBUG) {
    printf("!!!cache_flush!!!\n");
  }
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
  if (DEBUG) {
    printf("ENTER cache_lock\n");
  }
  try_again:
  lock_acquire(&cache_sync);
  /* Is the block already in-cache? */
  struct cache_block *cb;
  cb = in_cache(sector);
  if (cb != NULL) {
    cb->sector = sector;
    add_lock(cb, type);
    lock_release(&cache_sync); 
    return cb;
  } else {
    /* Not in cache.  Find empty slot. */
    cb = find_empty(); 
    if (cb != NULL) {
      cb->sector = sector;
      add_lock(cb, type); 
      lock_release(&cache_sync);
      return cb;
    } else {
      /* No empty slots.  Evict something. */
      cb = try_to_empty();
      if (cb != NULL) {
        cb->sector = sector;
        cb->up_to_date = false;
        add_lock(cb, type);
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
  lock_acquire(&b->data_lock);
  if (b->up_to_date) {
    if (DEBUG) {
      printf("cache_read from memory\n");
    }
    lock_release(&b->data_lock);
    return &b->data;
  } else {
    if (DEBUG) {
      printf("cache_read from disk\n");
    }
    b->up_to_date = true; 
    block_read(fs_device, b->sector, &b->data);
    lock_release(&b->data_lock);
    return &b->data;
  }
}

//Must have exclusive lock on block
void
cache_write(struct cache_block *b, void *data, off_t size, off_t offset) 
{
  if (DEBUG) {
    printf("cache_write sector %d\n", b->sector);
  }
  lock_acquire(&b->data_lock);
  b->up_to_date = true;
  b->dirty = true; 
  memcpy((void *)&b->data + offset, data, size); 
  lock_release(&b->data_lock);
}

/* Zero out block B, without reading it from disk, and return a
   pointer to the zeroed data.
   The caller must have an exclusive lock on B. */
void *
cache_zero (struct cache_block *b) 
{
  if (lock_held_by_current_thread(&b->block_lock)) {
    lock_acquire(&b->data_lock);
    memset(b->data, 0, BLOCK_SECTOR_SIZE);    
    lock_release(&b->data_lock);
    return b->data;
  }
  return NULL;
}

/* Marks block B as dirty, so that it will be written back to
   disk before eviction.
   The caller must have a read or write lock on B,
   and B must be up-to-date. */
void
cache_dirty (struct cache_block *b) 
{
  lock_acquire(&b->block_lock);
  b->dirty = true;
  lock_release(&b->block_lock);
}

/* Unlocks block B.
   If B is no longer locked by any thread, then it becomes a
   candidate for immediate eviction. */
void
cache_unlock (struct cache_block *b) 
{
  if (!lock_held_by_current_thread(&b->block_lock)) {
    //non-exclusive lock
    lock_acquire(&b->block_lock);
    b->readers -= 1;
    //clear_block(b);
    lock_release(&b->block_lock);
  } else {
    //exclusive lock
    b->writers -= 1;
    //clear_block(b);
    lock_release(&b->block_lock);
  }
}

/* If SECTOR is in the cache, evicts it immediately without
   writing it back to disk (even if dirty).
   The block must be entirely unused. */
void
cache_free (block_sector_t sector) 
{
  struct cache_block *cb =  in_cache(sector);
  if (cb != NULL) {
    lock_acquire(&cb->block_lock);
    clear_block(cb);
    lock_release(&cb->block_lock);
  }
}



bool 
full_read(block_sector_t sector,uint8_t *buffer, off_t size, off_t offset)
{
  if (DEBUG) {
    printf("full_read sector %d\n", sector);
  }
  struct cache_block *cb = cache_lock(sector, NON_EXCLUSIVE);
  if (cb != NULL) {
    memcpy(buffer, cache_read(cb) + offset, size);
    cache_unlock(cb);
    return true;
  } else {
    PANIC("buffer cache full_read failure\n");
  }
}

bool 
full_write(block_sector_t sector,uint8_t *buffer, off_t size, off_t offset)
{
  if (DEBUG) {
    printf("full_write sector %d\n", sector);
  }
  struct cache_block *cb = cache_lock(sector, EXCLUSIVE);
  if (cb != NULL) {
    if (offset == 0 && size == BLOCK_SECTOR_SIZE) {
      cache_write(cb, buffer, BLOCK_SECTOR_SIZE, offset); 
    } else {
      cache_read(cb);
      cache_write(cb, buffer, size, offset);
    }
    cache_unlock(cb);
    return true;
  } else {
    PANIC("buffer cache full_write failure\n"); 
  }
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
  for (;;) {
    timer_msleep (30 * 1000);
    cache_flush ();
  }
}

