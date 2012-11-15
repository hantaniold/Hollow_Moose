#include <stdbool.h>
#include <stddef.h>
#include "vm/swap.h"
#include "devices/block.h"
#include "bitmap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

static struct bitmap * swap_table;
static struct lock swap_lock;
static struct block * swap_block;

#define PAGE_SECTORS (PGSIZE / BLOCK_SECTOR_SIZE)

static void swap_write (size_t sector_nr, void * p);
// Find space for the page on disk (8 sectors), write it to disk.
bool 
swap_out (void * p) 
{
  size_t swap_sector = 0;

  lock_acquire (&swap_lock);
  if ((swap_sector == bitmap_scan (swap_table, 0, 1, false)) == SIZE_MAX) 
  {
    lock_release (&swap_lock);
    return false;
  }
  lock_release (&swap_lock);
  swap_write (swap_sector, p);
  return true;
}


// Read data, clear swap and bitmap
bool
swap_read (void * p)
{
}
// Given a "sector_nr" (which is actually indexing groups of PAGE_SECTORS
// sectors, write p's datap ortion to disk and set it
//
static void
swap_write (size_t sector_nr, void * p)
{
  // Some attribute of p should have PAGE_SIZE bytes.
  // p->sector_nr = sector_nr;
  const void * data = p;
  int i;
  for (i = 0; i < PAGE_SECTORS; i++) 
  {
    // There is internal synch.  in this call so we don't lock
    block_write (swap_block, sector_nr*PAGE_SECTORS + i, data + BLOCK_SECTOR_SIZE*i);
  }
}

void
swap_init(void) 
{
  swap_block = block_get_role (BLOCK_SWAP);
  if (swap_block == NULL) 
  {
    printf ("NO SWAP!!!! SWAP DISABLED\n");
    swap_table = bitmap_create (0);
  }
  else 
  {
    block_sector_t nr_swap_sectors  = block_size (swap_block);
    // Dear god note that 8 block sectors = 1 page sector
    swap_table = bitmap_create (nr_swap_sectors / PAGE_SECTORS);
    lock_init (&swap_lock);
  }

}







