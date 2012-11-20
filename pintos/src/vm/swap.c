#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include "vm/swap.h"
#include "devices/block.h"
#include "bitmap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/page.h"

static struct bitmap * swap_table;
static struct lock swap_lock;
static struct block * swap_block;

#define PAGE_SECTORS (PGSIZE / BLOCK_SECTOR_SIZE)

static void swap_write (size_t sector_nr, struct page * p);

// Find space for the page on disk (8 sectors), write it to disk.
// Update the swap table.
// Needs to be called for a page being evicted.
// After writing, remove page from frame table
bool 
swap_out (struct page * p) 
{
  size_t swap_sector = 0;

  lock_acquire (&swap_lock);
  if ((swap_sector = bitmap_scan_and_flip (swap_table, 0, 1, false)) == SIZE_MAX) 
  {
    lock_release (&swap_lock);
    PANIC("SWAP FULL!!!\n");
    return false;
  }
  lock_release (&swap_lock);
  swap_write (swap_sector, p);
  return true;
}


// Read data, clear swap table, reset p's setctor n
// You should update the global frame table after this
void
swap_read (struct page * p)
{
  int i;
  for ( i = 0; i < PAGE_SECTORS; i++)
  {
    block_read (swap_block, p->sector*PAGE_SECTORS + i, p->frame->base + BLOCK_SECTOR_SIZE*i);
  }
  bitmap_flip(swap_table,p->sector); 
  p->sector = -1;
}
// Given a "sector_nr" (which is actually indexing groups of PAGE_SECTORS
// sectors, write p's datap ortion to disk and set it
static void
swap_write (size_t sector_nr, struct page * p)
{
  p->sector = sector_nr;
  int i;
  for (i = 0; i < PAGE_SECTORS; i++) 
  {
    // There is internal synch.  in this call so we don't lock
    block_write (swap_block, sector_nr*PAGE_SECTORS + i, p->frame->base + BLOCK_SECTOR_SIZE*i);
  }
}

void
swap_init(void) 
{
  printf ("Getting Block %d\n",BLOCK_SWAP);
  swap_block = block_get_role (BLOCK_SWAP);
  lock_init (&swap_lock);
  if (swap_block == NULL) 
  {
    printf ("NO SWAP!!!! SWAP DISABLED\n");
    swap_table = bitmap_create (0);
  }
  else 
  {
    block_sector_t nr_swap_sectors  = block_size (swap_block);
    // Dear god note that 8 block sectors = 1 page sector
    printf ("nr swap sectors: %d, page_sectors: %d\n",nr_swap_sectors,PAGE_SECTORS);
    swap_table = bitmap_create (nr_swap_sectors / PAGE_SECTORS);
  }

}







