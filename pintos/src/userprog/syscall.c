#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
static int sys_open (const char * file);
static int sys_write (int fd, const void * buffer, unsigned size);

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
    default:
      break;
  }
  f->eax = retval;
}

static int sys_open (const char * file) 
{
}

// Writes SIZE bytes from BUFFER into the open file FD. Returns the number of
// bytes actually written, possibly less than SIZE.
static int sys_write (int fd, const void * buffer, unsigned size)
{
  int bytes_written;
  if (fd == STDIN_FILENO)
  {

  }
  // Write to terminal
  else if (fd == STDOUT_FILENO) 
  {
    // Break up larger size things later
    putbuf(buffer, size)
    bytes_written = size;
  }


  return bytes_written;
  
}


