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
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static int sys_open (const char * file);
static int sys_write (int fd, const void * buffer, unsigned size);
static void sys_exit (int status);
static void sys_halt (void);
static int sys_exec(const char *);
static int sys_wait(pid_t);
static void sys_close (int fd);
static int sys_read (int fd, void * buffer,unsigned size);
static bool sys_create (const char *file, unsigned initial_size);
static int sys_filesize (int fd);
static unsigned sys_tell (int fd);
static void sys_seek (int fd, unsigned position);

static void copy_in (void *dst_, const void *usrc_, size_t size);
static char * copy_in_string (const char *us);

static bool show_syscall;
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



  if ((uint32_t) f->esp <= 0x08048000 || ((PHYS_BASE - 4) <= f->esp )) {
    f->eax = -1;
    sys_exit(-1);
  } else {
    copy_in (&call_nr, f->esp, sizeof call_nr);

    // We know there are only 3 args max to a system call, fetch 'em later
    // If they're pointers we'll just deference them later
    copy_in (args, (uint32_t *) f->esp + 1, 12);

    int retval = 0;
    show_syscall = false;
    if (show_syscall)  printf ("Entering syscall \n");
    switch (call_nr) {
      case SYS_WRITE:
        if (show_syscall) printf( "WRITE!\n");
        retval = sys_write(args[0],(const void *) args[1], (unsigned) args[2]);
        break;
      case SYS_HALT:
        if (show_syscall) printf( "HALT!\n");
        sys_halt ();
        break;
      case SYS_EXIT:
        if (show_syscall) printf( "EXIT!\n");
        sys_exit (args[0]);
        break;
      case SYS_CREATE:
        if (show_syscall) printf ("CREATE!\n");
        retval = sys_create ((const char *) args[0], (unsigned) args[1]);
        break;
      case SYS_OPEN:
        if (show_syscall) printf( "OPEN!\n");
        retval = sys_open ((const char *) args[0]);
        break;
      case SYS_EXEC:
        if (show_syscall) printf( "EXEC!\n");
        retval = sys_exec ((const char *) args[0]);
        break;
      case SYS_WAIT:
        if (show_syscall) printf( "WAIT!\n");
        retval = sys_wait ((pid_t) args[0]);
        break;
      case SYS_CLOSE:
        if (show_syscall) printf( "CLOSE!\n");
        sys_close ((int) args[0]);
        break;
      case SYS_READ:
        if (show_syscall) printf( "READ!\n");
        retval = sys_read ((int) args[0], (void *) args[1], (unsigned) args[2]);
        break;
      case SYS_FILESIZE:
        if (show_syscall) printf( "FILESIZE!\n");
        retval = sys_filesize ((int) args[0]);
        break;
      case SYS_SEEK:
        if (show_syscall) printf( "SEEK!\n");
        sys_seek ((int) args[0], (unsigned) args[1]);
        break;
      case SYS_TELL:
        if (show_syscall) printf( "TELL!\n");
        retval = (int) sys_tell ((int) args[0]);
        break;
      case SYS_REMOVE:
        if (show_syscall) printf( "REMOVE!\n");
        break;
      default:
        break;
     }
    f->eax = retval;
  }

}

static void
sys_seek (int fd, unsigned position)
{
  thread_fs_lock ();
  struct file * fp = thread_get_file (fd);
  if (fp == NULL) return;
  file_seek (fp, position);
  thread_fs_unlock ();
  return;
  
}


static unsigned
sys_tell (int fd)
{
  thread_fs_lock ();
  struct file * fp = thread_get_file (fd);
  int pos = 0;
  if (fp == NULL)
  {
    return 0;
  }
  pos = file_tell(fp);
  thread_fs_unlock ();

  return pos;
}
// Returns -1 if doesnt belong to thread
static int
sys_filesize (int fd)
{
  struct file * fp = thread_get_file (fd);
  if (fp == NULL) return -1;
  return  file_length (fp);
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
  thread_fs_lock ();
  retval = filesys_create (new_filename,initial_size);
  thread_fs_unlock ();
  palloc_free_page (new_filename);
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
  char * kfile = copy_in_string (file);
  struct file * f;

  thread_fs_lock();
  f = filesys_open (kfile);
  thread_fs_unlock();
  if (f == NULL)
  {
    return -1;
  }
  
  int fd = thread_get_new_fd(f);

  palloc_free_page (kfile);
  return fd;
}

// Try to close this fd. Fail silently if doesn't belong to thread,
// try to close stdin/stdout. On success, closes the file 
static void
sys_close (int fd)
{
  thread_fs_lock ();
  struct file * fp = thread_close_fd (fd);
  if (fp != NULL) file_close(fp);
  thread_fs_unlock ();
}


// Rads SIZE bytes from the file open as FD into BUFFER. Returns the number of
// bytes actually read (0 at EOF), or -1 if the file could not be read due
// to some other condition. FD 0 reads from keyboard using input_getc ()
static int
sys_read (int fd, void * buffer, unsigned size)
{
  // If fd not belong to thread ERROR
  
  if (fd == 1) return -1; // stdout
  if (fd == 0) 
  {
    unsigned i;
    char * char_buf = (char *) buffer;
    for (i = 0; i < size; i++)
    {
      char_buf[i] = input_getc ();
    }
    return (int) size;
  } 
  else 
  {
    struct file * fp;
    if (NULL == (fp = thread_get_file (fd))) return -1;

    if ((uint32_t) buffer <= 0x08048000 || ((PHYS_BASE - 4) <= buffer ))
    {
      sys_exit(-1);         
    } 
    else
    {
      int bytes_read;
      thread_fs_lock ();
      bytes_read = file_read (fp, buffer, size);
      thread_fs_unlock ();
      return bytes_read;
    }  
  }
}
// Writes SIZE bytes from BUFFER into the open file FD. Returns the number of
// bytes actually written, possibly less than SIZE.
static int 
sys_write (int fd, const void * user_buf, unsigned size)
{
  // Get the data to write from user memory

  if (show_syscall)  printf ("WRITEINng to fd %d\n",fd);

  char * data =  copy_in_string((const char *)user_buf);
  int bytes_written = 0;
  if (fd == STDIN_FILENO)
  {
    return 0;
  }
  // Write to terminal
  else if (fd == STDOUT_FILENO) 
  {
    // Break up larger size things later
    putbuf(data, size);
    bytes_written = size;
  }
  else 
  {
  
    thread_fs_lock ();
    struct file *target = thread_get_file(fd);
    struct thread *t = thread_current();
    struct file *executable = filesys_open(t->name);

    if (same_file(target, executable)) {
      return 0;
      printf("WRITING TO EXECUTABLE\n");
    }
    
    if (show_syscall) printf ("WRITE: file pointer is %x\n",target);
    if (target == NULL) return 0;
    bytes_written = file_write (target, data, size);
    thread_fs_unlock ();
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
  {
    thread_exit ();
  }
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
