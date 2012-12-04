#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "filesys/filesys.h"

/* Type of block lock. */
enum lock_type 
  {
    NON_EXCLUSIVE,	/* Any number of lockers. */
    EXCLUSIVE		/* Only one locker. */
  };

void cache_init (void);
void cache_flush (void);
struct cache_block *cache_lock (block_sector_t, enum lock_type);
void *cache_read (struct cache_block *);
void *cache_zero (struct cache_block *);
void cache_dirty (struct cache_block *);
void cache_unlock (struct cache_block *);
void cache_free (block_sector_t);
void cache_readahead (block_sector_t);

void cache_write(struct cache_block *b, void *data, off_t size, off_t offset);

#endif /* filesys/cache.h */
