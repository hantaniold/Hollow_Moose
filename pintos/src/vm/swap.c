#include <stdbool.h>
#include "vm/swap.h"
#include "devices/block.h"
#include "bitmap.h"

static struct bitmap * swap_table;


/* Find an empty sector to be used to be swapped
 * via lookup into the swap table. Return -1
 * on failure, the sector number otherwise */
int32_t
swap_find_empty_sector (void) 
{
  const struct bitmap * st = swap_table;
  int i;
  int32_t entries = bitmap_size(swap_table);
  // LINEAR TIME BABY
  for (i = 0; i < entries; i++) 
  {
    if (false == bitmap_test(swap_table, i))
    {
      return i;
    }
  }
  return -1;
}


/* Write to the swap?? */
bool
swap_write(int32_t sector_nr)
{
  // Somehow write to swap
  // Return sector blah blah
  return false;
}

void
swap_init(void) 
{
  struct block * swap = block_get_role (BLOCK_SWAP);
  block_sector_t nr_swap_sectors  = block_size (swap);
  swap_table = bitmap_create (nr_swap_sectors);

}







