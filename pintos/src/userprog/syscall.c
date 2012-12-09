#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "filesys/inode.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "filesys/free-map.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
static int sys_open (const char * file);
static int sys_remove (const char * file);
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

//ADDED FOR FILESYS PROJECT 4
static bool sys_chdir (const char *dir);
static bool sys_mkdir (const char *dir);
static bool sys_readdir (int fd, char *name);
static bool sys_isdir (int fd);
static int  sys_inumber(int fd);

static void copy_in (void *dst_, const void *usrc_, size_t size);
static char * copy_in_string (const char *us);
static char * copy_in_data (const char *us, unsigned);


static bool put_bytes(uint8_t *udst, uint8_t *bytes, uint32_t size);

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
        retval = (int) sys_remove((const char *) args[0]);
        break;
      case SYS_CHDIR:
        if (show_syscall) printf("CHDIR!\n");
        retval = (int)sys_chdir((const char *)args[0]);
        break;
      case SYS_MKDIR:
        if (show_syscall) printf( "MKDIR!\n");
        retval = (int)sys_mkdir((const char *)args[0]);
        break;
      case SYS_READDIR:
        if (show_syscall) printf( "READDIR!\n");
        retval = (int)sys_readdir((int)args[0], (char *)args[1]);
        break;
      case SYS_ISDIR:
        if (show_syscall) printf("ISDIR!\n");
        retval = (int)sys_isdir((int)args[0]);
        break;
      case SYS_INUMBER:
        if (show_syscall) printf( "INUMBER!\n");
        retval = sys_inumber((int)args[0]);
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
 
  struct thread *t = thread_current();
  bool retval = false;
  char *new_filename =  copy_in_string (file);
  char *filename_cp = copy_in_string(file);

  ASSERT(new_filename != NULL);
  ASSERT(filename_cp != NULL);



  struct dir *curr_root = dir_reopen(t->wd);
  if (new_filename[0] == '/') {
    dir_close(curr_root);
    curr_root = dir_open_root();
  }
  char *token;
  const char *delim = "/";
  char *saveme;
  bool first = true;
 

  //printf("CREATING DIR %s\n", kdir);
  int count = 0;
  while (1) {
    if (first) {
      token = strtok_r(new_filename, delim, &saveme);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme);
    }
    if (token != NULL) {
      count += 1; 
    } else {
      break;
    }
  } 

  first = true;
  char *saveme2;
  int counter2 = 0;
  while (1) {
    if (first) {
      token = strtok_r(filename_cp, delim, &saveme2);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme2); 
    }
    if (token != NULL) {
      counter2 += 1;
      if (counter2 < count) {
        //chdir
        struct inode *inode_next;
        bool lookup = dir_lookup(curr_root, token, &inode_next);
        if (lookup) {
          dir_close(curr_root);
          curr_root = dir_open(inode_next);   
        } else {
          dir_close(curr_root);
          palloc_free_page (filename_cp);
          palloc_free_page (new_filename);
          return false;
        }
      } else {
        //mk file
        thread_fs_lock ();
        retval = filesys_create_with_dir (curr_root, token,initial_size,FILE_INODE);
        thread_fs_unlock ();
        

        dir_close(curr_root);
        palloc_free_page (filename_cp);
        palloc_free_page (new_filename);
        return retval; 
      }
    } else {
      break;
    }
  }
  //this code is never reached
  /*
  thread_fs_lock ();
  retval = filesys_create_with_dir (curr_root, rew_filename,initial_size,FILE_INODE);
  thread_fs_unlock ();

  dir_close(curr_root);
  palloc_free_page (filename_cp);
  palloc_free_page (new_filename);
  return retval;
  */
  return false;
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
  if ((uint32_t) file <= 0x08048000 || ((PHYS_BASE - 4) <= file ))
  {
    sys_exit(-1);
  }

  
  char * kfile = copy_in_string (file);
  char * kfile_cp = copy_in_string(file);

  struct thread *t = thread_current();

  struct dir *curr_root = dir_reopen(t->wd);
  if (kfile[0] == '/') {
    dir_close(curr_root);
    curr_root = dir_open_root();
  }
  
  if (strcmp(kfile, "/") == 0) {
    //printf("JUMPED HERE\n");
    thread_fs_lock();
    
    struct directory *d = dir_open_root();
    
    thread_fs_lock();
    struct file *f = file_open(inode_reopen(dir_get_inode(d)));
    thread_fs_unlock();

    int fd = thread_get_new_fd(f);

    dir_close(d);
    dir_close(curr_root);
    palloc_free_page (kfile_cp);
    palloc_free_page (kfile);
    return fd;
  }
  
  
  char *token;
  const char *delim = "/";
  char *saveme;
  bool first = true;
  
  //printf("OPEN FILE %s\n", kfile);
  int count = 0;
  while (1) {
    if (first) {
      token = strtok_r(kfile, delim, &saveme);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme);
    }
    if (token != NULL) {
      count += 1;
      //printf("I GOT TOKEN %s\n", token);
    } else {
      break;
    }
  }

  first = true;
  char *saveme2;
  int count2 = 0;
  while(1) {
    if (first) {
      token = strtok_r(kfile_cp, delim, &saveme2);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme2);
    }
    if (token != NULL) {
      count2 += 1;
      if (count2 < count) {
        if (strcmp(token, "..") == 0) {
          //go up
          struct inode *up_node;
          bool up_success = dir_lookup(curr_root, "..", &up_node);  
          if (up_success && up_node != NULL) {
            dir_close(curr_root);
            curr_root = dir_open(up_node);
          } else {
            dir_close(curr_root);
            palloc_free_page (kfile_cp);
            palloc_free_page (kfile);
            return false;
          }
        } else if (strcmp(token, ".") == 0) {
          //stay where you are - do nothing
          //dir_close(curr_root);
        } else {
          struct inode *inode_next;
          bool lookup = dir_lookup(curr_root, token, &inode_next);
          if (lookup) {
            dir_close(curr_root);
            curr_root = dir_open(inode_next);   
          } else {
            dir_close(curr_root);
            palloc_free_page (kfile_cp);
            palloc_free_page (kfile);
            return false;
          }
        }
      } else {
        if (strcmp(token, ".") == 0) {
          thread_fs_lock();
          ASSERT(curr_root != NULL);
          struct inode *node = dir_get_inode(curr_root); 
          ASSERT(node != NULL);          
          struct inode *node2 = inode_reopen(node);
          //ASSERT(node2 != NULL);
          struct file *f = file_open(node2);
          ASSERT(f != NULL);
          thread_fs_unlock();
          int fd = thread_get_new_fd(f);
          dir_close(curr_root);
          palloc_free_page (kfile_cp);
          palloc_free_page (kfile);
          return fd;
        }             
        struct file * f;
        thread_fs_lock();
        f = filesys_open_with_dir(curr_root, token);
        thread_fs_unlock();
        if (f == NULL)
        {
          dir_close(curr_root);
          palloc_free_page (kfile_cp);
          palloc_free_page (kfile);
          return -1;
        }
        
        int fd = thread_get_new_fd(f);
        dir_close(curr_root);
        palloc_free_page (kfile_cp);
        palloc_free_page (kfile);
        return fd;
      }
    } else {
      break;
    }
    
  }
}

// Try to close this fd. Fail silently if doesn't belong to thread,
// try to close stdin/stdout. On success, closes the file 
static void
sys_close (int fd)
{
  thread_fs_lock ();
  
  struct file *target = thread_get_file(fd);
  if (target == NULL) {
    thread_fs_unlock();
  }  
  struct inode *check_inode = file_get_inode(target);
 
  if (check_inode == NULL) {
    thread_fs_unlock();
  }

  if (inode_get_type(check_inode) == DIR_INODE) {
    struct file *fp = thread_close_fd(fd);
    file_close(fp);
    thread_fs_unlock(); 
  } else {
    struct file * fp = thread_close_fd (fd);
    if (fp != NULL) file_close(fp);
    thread_fs_unlock (); 
  }
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
    int bytes = file_read(fp, buffer, size);
    return bytes;
    /*
    else
    {
      int bytes_read;
      
      unsigned left_to_read = size;
      int bytes_read_cumulative = 0;
      
      char *ks = palloc_get_page(PAL_ZERO);
      if (ks == NULL) {
        return -1;
      }
      unsigned t_size;
      //thread_fs_lock();
      printf("BEFORE WHILE\n");
      while (left_to_read > 0)
      {
        t_size = left_to_read > PGSIZE ? PGSIZE : left_to_read;
        printf("t_size %d\n", t_size);
        bytes_read = file_read(fp, ks, t_size);
        put_bytes(((char *)buffer) + bytes_read_cumulative, ks ,t_size);
        
        left_to_read -= bytes_read;
        bytes_read_cumulative += bytes_read;
      }
      //thread_fs_unlock();
      palloc_free_page(ks); 
      return bytes_read_cumulative;
    } 
    */
  }

  return -1;
}
// Writes SIZE bytes from BUFFER into the open file FD. Returns the number of
// bytes actually written, possibly less than SIZE.
static int 
sys_write (int fd, const void * user_buf, unsigned size)
{
  // Get the data to write from user memory

  if (show_syscall)  printf ("WRITEINng to fd %d\n",fd);

  
  int bytes_written = 0;
  if (fd == STDIN_FILENO)
  {
    return 0;
  }
  // Write to terminal
  else if (fd == STDOUT_FILENO) 
  {
    char * data =  copy_in_string((const char *)user_buf);
    // Break up larger size things later
    putbuf(data, size);
    bytes_written = size;
  }
  else 
  {
    if ((uint32_t) user_buf <= 0x08048000 || ((PHYS_BASE - 4) <= user_buf ))
    {
      sys_exit(-1);         
    }
    thread_fs_lock ();
    struct file *target = thread_get_file(fd);
    struct thread *t = thread_current();
    
    struct inode *check_inode = file_get_inode(target);
    
    if (inode_get_type(check_inode) != FILE_INODE) {
      thread_fs_unlock();
      return -1;
    }


    if (show_syscall) printf ("WRITE: file pointer is %x\n",target);
    if (target == NULL) return 0;
    
    unsigned write_count = size;
    int bytes_written_temp;
    unsigned write_count_last = 0;
    bool first = true;
    while (write_count > 0)
    {
      unsigned write_size = write_count > PGSIZE ? PGSIZE : write_count;
      char * data =  copy_in_data(user_buf + bytes_written, write_size);
      bytes_written_temp = file_write (target, data, write_size);
      bytes_written += bytes_written_temp;
      write_count -= bytes_written_temp;
      if (!first) 
      {
        if (write_count_last == write_count) {
          break;
        }
      } else {
        first = false;
      }
      write_count_last = write_count;
      palloc_free_page(data);
    }
    thread_fs_unlock ();
  }
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

static bool
put_bytes(uint8_t *udst, uint8_t *bytes, uint32_t size)
{
  bool output = true;

  int i;
  for (i = 0; i < size; i++) 
  {
    put_user(udst + i, *(bytes + i));
  }

  return output;
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



static char *
copy_in_data (const char *us, unsigned write_size)
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
  while ((counter < write_size) && (r_val)) {
    r_val = get_user((uint8_t *) char_ptr, (const uint8_t *) us_ptr);    
    char_ptr++;
    us_ptr++; 
    counter++;
  }


  if (!r_val) {
    //segfault
    thread_exit();
  }

  return ks;
;
  // don't forget to call palloc_free_page(..) when you're done
  // with this page, before you return to user from syscall
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

static int 
sys_remove (const char * file)
{
  if ((uint32_t) file <= 0x08048000 || ((PHYS_BASE - 4) <= file )) {
    sys_exit(-1);         
  } 
  char * kfile = copy_in_string (file); 
  char * kfile_cp = copy_in_string(file); 
   
  struct thread *t = thread_current();

  struct dir *curr_root = dir_reopen(t->wd);
  if (kfile[0] == '/') {
    dir_close(curr_root);
    curr_root = dir_open_root();
  }
  char *token;
  const char *delim = "/";
  char *saveme;
  bool first = true;
  
  //printf("CREATING DIR %s\n", kdir);
  int count = 0;
  while (1) {
    if (first) {
      token = strtok_r(kfile, delim, &saveme);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme);
    }
    if (token != NULL) {
      count += 1; 
    } else {
      break;
    }
  }

  first = false;
  char *saveme2;
  
  int count2 = 0;

  while (1) {
    if (first) {
      token = strtok_r(kfile_cp, delim, &saveme2);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme2);
    }
    if (token != NULL) {
      count2 += 1;
      if (count2 < count) {
        //chdir
        struct inode *inode_next;
        bool lookup = dir_lookup(curr_root, token, &inode_next);
        if (lookup) {
          dir_close(curr_root);
          curr_root = dir_open(inode_next);   
        } else {
          dir_close(curr_root);
          palloc_free_page (kfile);
          palloc_free_page (kfile_cp);
          return false;
        }
      } else {
        //rm
        bool o = filesys_remove_with_dir(curr_root, kfile);
        dir_close(curr_root);
        palloc_free_page(kfile_cp);
        palloc_free_page(kfile);
        return (int)o;
      }
    } else {
      break;
    }
  }
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


static bool 
sys_chdir (const char *dir)
{
  if ((uint32_t) dir <= 0x08048000 || ((PHYS_BASE - 4) <= dir )) {
    sys_exit(-1);         
  } 
  char * kdir = copy_in_string (dir); 


  char *token;
  const char *delim = "/";
  char *saveme;
  bool first = true;
  struct thread *t = thread_current();
  struct dir *curr_root = dir_reopen(t->wd); 
  if (kdir[0] == '/') {
    dir_close(curr_root);
    curr_root = dir_open_root();
  }
 

  while (1) {
    if (first) {
      token = strtok_r(kdir, delim, &saveme);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme);
    }
    if (token != NULL) {
      struct inode *inode_next;
      ASSERT(curr_root != NULL);
      bool lookup = dir_lookup(curr_root, token, &inode_next);
      if (lookup) {
        dir_close(curr_root);
        curr_root = dir_open(inode_next);   
      } else {
        dir_close(curr_root);
        palloc_free_page(kdir);
        return false;
      }
    } else {
      break;
    }
  }
  dir_close(t->wd);
  t->wd = curr_root;

  palloc_free_page(kdir);
  return true;
}

static bool 
sys_mkdir (const char *dir)
{
  if ((uint32_t) dir <= 0x08048000 || ((PHYS_BASE - 4) <= dir )) {
    sys_exit(-1);         
  } 
  char * kdir = copy_in_string (dir); 
  char * kdircp = copy_in_string(dir);
  if (strlen(kdir) < 1) {
    palloc_free_page(kdir);
    palloc_free_page(kdircp);
    return false;
  }

  char *token;
  const char *delim = "/";
  char *saveme;
  bool first = true;
  struct thread *t = thread_current();
  struct dir *curr_root = dir_reopen(t->wd); 
  if (kdir[0] == '/') {
    dir_close(curr_root);
    curr_root = dir_open_root();
  }
  //printf("CREATING DIR %s\n", kdir);
  int count = 0;
  while (1) {
    if (first) {
      token = strtok_r(kdir, delim, &saveme);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme);
    }
    if (token != NULL) {
      count += 1; 
    } else {
      break;
    }
  } 
  //printf("AFTER STRTOK\n");
  
  first = true;
  char *saveme2;

  int count2 = 0;
  while (1) {
    if (first) {
      token = strtok_r(kdircp, delim, &saveme2);
      first = false;
    } else {
      token = strtok_r(NULL, delim, &saveme2);
    }
    if (token != NULL) {
      count2 += 1;
      if (count2 < count) {
        //printf("CHANGE DIR\n");
        struct inode *inode_next;
        bool lookup = dir_lookup(curr_root, token, &inode_next);
        if (lookup) {
          dir_close(curr_root);
          curr_root = dir_open(inode_next);   
        } else {
          dir_close(curr_root);
          palloc_free_page(kdir);
          palloc_free_page(kdircp);
          return false;
        }
      } else {
        //printf("MKDIR DIR\n");
        struct inode *inode_next;
        //printf("BEFORE LOOKUP\n"); 
        bool lookup = dir_lookup(curr_root, token, &inode_next);
        //printf("AFTER LOOKUP\n"); 
        if (lookup) {
          dir_close(curr_root);
          palloc_free_page(kdir);
          palloc_free_page(kdircp);
          return false;
        } else {
          block_sector_t sector;
          bool alloc = free_map_allocate(1, &sector);
          if (!alloc) {
            dir_close(curr_root);
            palloc_free_page(kdir);
            palloc_free_page(kdircp);
            return false;
          }
          if (dir_create(sector, 32)) {
            bool output = dir_add(curr_root, token, sector);
            if (output) {
              //gotta add .. and .
              struct inode *n = inode_open(sector);
              struct dir *d = dir_open(n);
              dir_add(d, ".", sector);
              struct inode *root_inode = dir_get_inode(curr_root);
              block_sector_t root_sector = inode_get_inumber(root_inode);
              dir_add(d, "..", root_sector);
              dir_close(d);
              dir_close(curr_root);
              palloc_free_page(kdir);
              palloc_free_page(kdircp);
              return true;
            } else {
              dir_close(curr_root);
              palloc_free_page(kdir);
              palloc_free_page(kdircp);
              return output;
            }
          } else {
            dir_close(curr_root);
            palloc_free_page(kdir);
            palloc_free_page(kdircp);
            return false;
          }
        }
      }
    } else {
      break;
    }
  }

  dir_close(curr_root);
  palloc_free_page(kdir);
  palloc_free_page(kdircp);
  return true;
}

static bool 
sys_readdir (int fd, char *name)
{
  if ((uint32_t) name <= 0x08048000 || ((PHYS_BASE - 4) <= name )) {
    sys_exit(-1);         
  } 

  thread_fs_lock();

  struct file * fp;
  if (NULL == (fp = thread_get_file (fd))){
    thread_fs_unlock();
    return false; 
  }
  
  struct inode *check_inode = file_get_inode(fp);

  if (check_inode == NULL) {
    thread_fs_unlock();
    return false;
  }

  if (inode_get_type(check_inode) != DIR_INODE) {
    thread_fs_unlock();
    return false;
  }

  struct dir *d = dir_open(inode_reopen(check_inode));

  dir_set_pos(d, file_tell(fp));


  char *output = name;

  
  bool test = dir_readdir(d, output); 
  if (test) {
    if (strcmp(output, ".") == 0) {
      test = dir_readdir(d, output);
      if (test) {
        if (strcmp(output, "..") == 0) {
          test = dir_readdir(d, output);
          if (test) {
            //cp and return 
            int len = strlen(output);
            //put_bytes(name, output, len + 1);
            thread_fs_unlock();
            int p = dir_get_pos(d);
            file_seek(fp, p);
            dir_close(d);
            //palloc_free_page(output);
            return true;
          } else {
            //failure close
            thread_fs_unlock();
            dir_close(d);
            //palloc_free_page(output);
            return false;
          }
        } else {
          //cp and return
          int len = strlen(output);
          //put_bytes(name, output, len + 1);
          thread_fs_unlock();
          int p = dir_get_pos(d);
          file_seek(fp, p);
          dir_close(d);
          //palloc_free_page(output);
          return true;
        }
      } else {
        //failure close
        thread_fs_unlock();
        dir_close(d);
        //palloc_free_page(output);
        return false;
      }
    } else {
      //cp and return
      int len = strlen(output);
      //put_bytes(name, output, len + 1);
      thread_fs_unlock();
      int p = dir_get_pos(d);
      file_seek(fp, p);
      dir_close(d);
      //palloc_free_page(output);
      return true;
    }
  } else {
    thread_fs_unlock();
    dir_close(d);
    //palloc_free_page(output);
    return false;
  }
}

static bool 
sys_isdir (int fd)
{
  //printf("ENTER ISDIR\n");
  thread_fs_lock ();
  struct file *target = thread_get_file(fd);
  if (target == NULL) {
    thread_fs_unlock();
    return false;
  }  
   
  struct inode *check_inode = file_get_inode(target);
 
  if (check_inode == NULL) {
    thread_fs_unlock();
    return false;
  }

  //printf("ISDIR OUTPUT: inode_sector %d\n", inode_get_inumber(check_inode));

  bool o = inode_get_type(check_inode) == DIR_INODE;
  thread_fs_unlock();

  return o;
}

static int
sys_inumber(int fd)
{
  thread_fs_lock ();
  struct file *target = thread_get_file(fd);
  if (target == NULL) {
    thread_fs_unlock();
    return -1;
  }  
  struct inode *check_inode = file_get_inode(target);
 
  if (check_inode == NULL) {
    thread_fs_unlock();
    return -1;
  }
  block_sector_t o = inode_get_inumber(check_inode);
  thread_fs_unlock();

  return (int)o;
}


