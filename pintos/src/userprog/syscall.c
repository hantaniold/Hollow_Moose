#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
static int sys_open (const char * file);
static int sys_write (int fd, const void * buffer, unsigned size);

static void copy_in (void *dst_, const void *usrc_, size_t size);
static char * copy_in_string (const char *us);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned call_nr;
  int args[3]; // 3 args max
  memset (args, 0, sizeof args);

  copy_in (&call_nr, f->esp, sizeof call_nr);

  // We know there are only 3 args max to a system call, fetch 'em later
  // If they're pointers we'll just deference them later
  copy_in (args, (uint32_t *) f->esp + 1, 3);

  int retval;
  switch (call_nr) {
    case SYS_WRITE:
      retval = sys_write(args[0],(const void *) args[1], (unsigned) args[2]);
      break;
    case SYS_HALT:
      break;
    case SYS_EXIT:
      exit(args[0]);
    default:
      break;
  }
  f->eax = retval;
}

static int 
sys_open (const char * file UNUSED)
{
}

// Writes SIZE bytes from BUFFER into the open file FD. Returns the number of
// bytes actually written, possibly less than SIZE.
static int 
sys_write (int fd, const void * user_buf, unsigned size)
{
  printf("In write\n");
  // Get the data to write from user memory
  char * data =  copy_in_str(user_buf);
  int bytes_written;
  if (fd == STDIN_FILENO)
  {

  }
  // Write to terminal
  else if (fd == STDOUT_FILENO) 
  {
    // Break up larger size things later
    putbuf(data, size);
    bytes_written = size;
  }

  // Free the page
  palloc_free_page(data);
  return bytes_written;
}
  
/* Copies a byte from user address USRC to kernel address DST.  USRC must
 * be below PHYS_BASE.  Returns true if successful, false if a segfault
 * occurred. Unlike the one posted on the p2 website, thsi one takes two
 * arguments: dst, and usrc */


static inline bool
get_user (uint8_t *dst, const uint8_t *usrc)
{
  int eax;
  asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
       : "=m" (*dst), "=&a" (eax) : "m" (*usrc));
  return eax != 0;
}


/* Writes BYTE to user address UDST.  UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */

static inline bool
put_user (uint8_t *udst, uint8_t byte)
{
  int eax;
  asm ("movl $1f, %%eax; movb %b2, %0; 1:"
       : "=m" (*udst), "=&a" (eax) : "q" (byte));
  return eax != 0;
}



/* Copies SIZE bytes from user address USRC to kernel address DST.  Call
   thread_exit() if any of the user accesses are invalid. */ 

static 
void copy_in (void *dst_, const void *usrc_, size_t size) { 

  uint8_t *dst = dst_; 
  const uint8_t *usrc = usrc_;
  
  for (; size > 0; size--, dst++, usrc++)
    if (usrc >= (uint8_t *) PHYS_BASE || !get_user (dst, usrc))
      thread_exit ();
}



/* Creates a copy of user string US in kernel memory and returns it as a
   page that must be freed with palloc_free_page().  Truncates the string
   at PGSIZE bytes in size.  Call thread_exit() if any of the user accesses
   are invalid. */
static char *
copy_in_string (const char *us)
{
  char *ks;
  size_t length;

  ks = palloc_get_page (0);
  if (ks == NULL)
    thread_exit ();

  uint32_t counter = 0;
  bool r_val;
  while (counter < PGSIZE && r_val && *(ks + counter) != '\0' ) {
    r_val = get_user((ks + counter), us);
    counter++;
  }

  if (!r_val) {
    //segfault
    thread_exit();
  }

  if (*(ks + counter - 1) != '\0') {
    //string was longer than a page
    *(ks + counter - 1) = '\0';
  }

  return ks;

  // don't forget to call palloc_free_page(..) when you're done
  // with this page, before you return to user from syscall
}

/* Exit system call. */
void 
exit (int status) {
  thread_exit();
}
