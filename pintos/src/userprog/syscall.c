#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static int sys_open (const char * file);
static int sys_write (int fd, const void * buffer, unsigned size);
static void sys_exit (int status);
static void sys_halt (void);
static int sys_exec(const char *);
static int sys_wait(pid_t);

static bool sys_create (const char *file, unsigned initial_size);

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



  if (f->esp <= 0x08048000 || ((PHYS_BASE - 4) <= f->esp )) {
    f->eax = -1;
    sys_exit(-1);
  } else {
    copy_in (&call_nr, f->esp, sizeof call_nr);
    int *t = f->esp;
    // We know there are only 3 args max to a system call, fetch 'em later
    // If they're pointers we'll just deference them later
    copy_in (args, (uint32_t *) f->esp + 1, 12);

    int retval = 0;
    switch (call_nr) {
      case SYS_WRITE:
        retval = sys_write(args[0],(const void *) args[1], (unsigned) args[2]);
        break;
      case SYS_HALT:
        sys_halt ();
        break;
      case SYS_EXIT:
        sys_exit(args[0]);
        break;
      case SYS_CREATE:
        retval = sys_create((const char *) args[0], (unsigned) args[1]);
        break;
      case SYS_OPEN:
        retval = sys_open((const char *) args[0]);
        break;
      case SYS_EXEC:
        retval = sys_exec((const char *) args[0]);
        break;
      case SYS_WAIT:
        retval = sys_wait((pid_t) args[0]);
        break;
      default:
        break;
     }
    f->eax = retval;
  }

}

static int
sys_exec(const char * cmd_line)
{
  return process_execute(cmd_line);
}

static int
sys_wait(pid_t pid)
{
  return process_wait(pid);
}

//Creates a new file called file initially initial_size bytes in size. 
//Returns true if successful, false otherwise. Creating a new file does 
//not open it: opening the new file is a separate 
//operation which would require a open system call.
static bool
sys_create (const char *file, unsigned initial_size) 
{
  bool retval = false;
  char *new_filename =  copy_in_string (file);
  retval = filesys_create (new_filename,initial_size);
  return retval;
}

static void
sys_halt (void) 
{
  shutdown_power_off ();
}


//Opens the file called file. Returns a nonnegative integer handle called a 
//"file descriptor" (fd), or -1 if the file could not be opened.
static int 
sys_open (const char * file)
{
  return -1;
  
  char * kfile = copy_in_string (file);

  struct file * f;
  f = filesys_open (kfile);
  // Make 
  if (f == NULL) return -1;

  // call filesys_open 

  return -1;
}

// Writes SIZE bytes from BUFFER into the open file FD. Returns the number of
// bytes actually written, possibly less than SIZE.
static int 
sys_write (int fd, const void * user_buf, unsigned size)
{
  // Get the data to write from user memory


  char * data =  copy_in_string(user_buf);
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

  ks = palloc_get_page (PAL_ZERO);
  if (ks == NULL)
    thread_exit ();

  if ((uint8_t *) us >= (uint8_t *)PHYS_BASE) {
    thread_exit();
  }

  uint32_t counter = 0;
  bool r_val = true;
  char *char_ptr = ks;
  const char *us_ptr = us;
  while ((counter < PGSIZE) && (r_val)) {
    r_val = get_user((uint8_t *) char_ptr, (const uint8_t *) us_ptr);    
    if (*(char_ptr) == '\0') {
      break;
    }
    char_ptr++;
    us_ptr++; 
    counter++;
  }


  if (!r_val) {
    //segfault
    thread_exit();
  }

  if (*(char_ptr) != '\0') {
    //string was longer than a page
    *(char_ptr) = '\0';
  }

  return ks;

  // don't forget to call palloc_free_page(..) when you're done
  // with this page, before you return to user from syscall
}

/* Exit system call. */
static void 
sys_exit (int status) {
  struct thread *t = thread_current();
  enum intr_level old_level;
  old_level = intr_disable();
  t->retval = status; 
  intr_set_level(old_level);
  thread_exit();
}
