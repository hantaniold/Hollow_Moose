#include "devices/block.h"
#include "vm/page.h"
#include <stddef.h>
bool swap_out (struct page * p);
void swap_read (struct page * p);
void swap_init (void);
