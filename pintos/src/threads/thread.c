#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* ADDED List of sleeping threads */
static struct list wait_list;

/* Added for process_wait */
/* Keeps track of things that recently  died */
static struct list dead_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;


/* File descriptor table. 
 * Each entry contains a pointer to the file struct
 * if existant, otherwise NULL. */
static struct file * fd_table[128];
#define FD_TABLE_LEN 128
static struct lock fs_lock;



/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  lock_init (&fs_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&dead_list);
  list_init (&wait_list);
  lock_init(&process_lock);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  int i;
  for (i =0; i < 128; i++) 
  {
    fd_table[i] = NULL;
  }
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();
  //printf ("NEW THREAD!!!! %d\n",tid);

  /* init FD list */
  int i;
  for (i = 0; i < FD_LIST_LEN; i++)
  {
    t->fd_list[i] = 0;
  }

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  t->wd = NULL;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}


/* Added for setting up processes with parents */
tid_t 
thread_create_with_parent(tid_t parent, 
                          const char *name, 
                          int priority,  
                          thread_func *function, 
                          void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  t->parent = parent;
  tid = t->tid = allocate_tid ();
  
  int i;
  for (i = 0; i < FD_LIST_LEN; i++)
  {
    t->fd_list[i] = 0;
  }

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}




/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Returns true if tid is a tid_t of a thread on the
 * readylist.  False otherwise.
 *
 * */

bool
on_ready_list(tid_t tid) 
{
  bool output = false;

  struct list_elem *e;

  for (e = list_begin(&ready_list); e!= list_end(&ready_list);
       e = list_next(e))
  {
    struct thread *t = list_entry(e, struct thread, elem);
    if (t->tid == tid) 
    {
      output = true;
    }  
  }

  return output;
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);

  t->child_count = 0;
  t->parent = NULL;
  
  list_init(&t->children);
}

//TODO - NEED TO FREE MEMORY

//adds a child to current_thread
void 
add_child(tid_t tid, const char * name) {
  enum intr_level old_level;
  old_level = intr_disable ();
  struct thread *t = thread_current();
  t->children[t->child_count].tid = tid;
  t->children[t->child_count].retval = 0;
  t->children[t->child_count].load_result = 0; 
  size_t len = strcspn(name, " ");
  strlcpy(t->children[t->child_count].name, name, len + 1);
  int index = t->child_count;
  t->child_count += 1;
  intr_set_level (old_level); 
  //intr_enable();
  /* 
  thread_fs_lock(); 
  t->children[index].exec_lock = filesys_open (t->children[index].name);
  if (t->children[index].exec_lock != NULL)
  {
    file_deny_write(t->children[index].exec_lock); 
  }
  thread_fs_unlock();
  */
}

void
remove_child(tid_t tid) {
  enum intr_level old_level;
  old_level = intr_disable ();
  struct thread *t = thread_current();
  int bound = t->child_count;
  int count = 0;
  int i = 0;
  for (i = 0; i < bound; i++) {
    child_thread_marker m = t->children[i];
    if (m.tid != tid) {
      t->children[count].tid = t->children[i].tid;
      t->children[count].retval = t->children[i].retval;
      size_t len = strcspn(t->children[i].name, " ");
      strlcpy(t->children[count].name, t->children[i].name, len + 1);   
      t->children[count].load_result = t->children[i].load_result;
      t->children[count].exec_lock = t->children[i].exec_lock;
      count++;
    }
  }
  t->child_count = count;
  intr_set_level (old_level);
}

struct thread *
get_thread_by_tid(tid_t tid) {
  enum intr_level old_level;
  old_level = intr_disable ();
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
  {
    struct thread *t = list_entry (e, struct thread, allelem);
    if (t->tid == tid) {
      intr_set_level(old_level);
      return t;
    }
  }
  intr_set_level(old_level);
  return NULL;
}

void
set_child_retval(struct thread *t, tid_t tid, int retval) 
{
  enum intr_level old_level;
  old_level = intr_disable ();
  int i;
  for (i = 0; i < t->child_count; ++i)
  {
    child_thread_marker m = t->children[i];
    if (m.tid == tid) {
      t->children[i].retval = retval;
    }
  }
  intr_set_level(old_level);
}

child_thread_marker
get_child_by_parent(tid_t parent, tid_t child) 
{
  enum intr_level old_level;
  old_level = intr_disable ();
  struct thread *t = get_thread_by_tid(parent);
  int i;
  for (i = 0; i < t->child_count; ++i)
  {
    child_thread_marker m = t->children[i];
    if (m.tid == child) {
      t->children[i].invalid = 0;
      return t->children[i];
    }
  }
  child_thread_marker m;
  m.invalid = 1;
  intr_set_level(old_level);
  return m;
}

child_thread_marker
get_child(tid_t tid)
{
  enum intr_level old_level;
  old_level = intr_disable ();
  struct thread *t = thread_current();
  intr_set_level(old_level);
  return get_child_by_parent(t->tid, tid);
}

child_thread_marker *
get_child_pointer_parent(tid_t parent, tid_t child)
{
  enum intr_level old_level;
  old_level = intr_disable ();
  struct thread *t = get_thread_by_tid(parent);
  int i;
  for (i = 0; i < t->child_count; ++i)
  {
    child_thread_marker m = t->children[i];
    if (m.tid == child) {
      t->children[i].invalid = 0;
      intr_set_level(old_level);
      return &(t->children[i]);
    }
  }
  intr_set_level(old_level);
  return NULL;
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);




struct file *  thread_get_file (int fd) 
{
  struct thread * t =  thread_current ();

  if (fd <= 1 || fd >= FD_LIST_LEN)
  {
    return NULL;
  }

  int i;
  for (i = 0; i < FD_LIST_LEN; i++) 
  {
    // If we have the descriptor then get its FP from the table
    if (t->fd_list[i] == fd)
    {
      return fd_table[fd];
    }
  }
  return NULL;
}

// Close the fd. If the thread doesn't own
// the fd or some other error, fail silently. Otherwise,
// grab the file pointer and set all fd_table entries to null
struct file * thread_close_fd (int fd)
{
  // These returns may be substituted by killing hte thread with exit(-1) 
  if (fd < 2 || fd > FD_TABLE_LEN) return NULL;

  bool found = false;
  struct thread *t = thread_current(); 
  int i;

  for (i = 0; i < FD_LIST_LEN; i++) {
    if (t->fd_list[i] == fd) {
      found = true;
      break;
    }
  }

  if (!found) {
    return NULL;
  }
 
  // Remove from this thread's fd list before rwe remove from
  // the file descriptor table
  for (i = 0; i < FD_LIST_LEN; i++)
  {
    if (t->fd_list[i] == fd) 
   {
      t->fd_list[i] = 0;
    }
  }
 

  struct file * fp = fd_table[fd];
  if (fp == NULL) return NULL;


  // Remove all refs from table
  for (i = 0; i < FD_TABLE_LEN; i++)
  {
    /*
    if (same_file(fd_table[i],fp)) 
    {
      fd_table[i] = NULL;
    }
    */
  }

  return fp;
}

/* Get an fd for this file handle, update the global fd table,
 * as well as the list of this thread's fds */
int thread_get_new_fd (struct file * f) 
{
  thread_fs_lock ();
  struct thread *t = thread_current ();

  int fd = -1;
  int i;
  // 0, 1 reserved for stdout/in
  // look for place to store new file pointer - store it, get key
  for (i = 2; i < FD_TABLE_LEN; i++) 
  {
    if (fd_table[i] == NULL) 
    {
      fd_table[i] = f;
      fd = i;
      break;
    }
  }
  //Find a spot to place fd into thread's fd_list
  for (i = 0; i < FD_LIST_LEN; i++) 
  {
    if (t->fd_list[i] == 0)
    {
      t->fd_list[i] = fd;
      break;
    }
  }
  thread_fs_unlock ();

  return fd;
}

void thread_fs_lock (void)
{
  if (!lock_held_by_current_thread(&fs_lock))
  {
    lock_acquire(&fs_lock);
  }
}

void thread_fs_unlock (void)
{
  lock_release(&fs_lock);
}


void
thread_wake_routine ()
{
    enum intr_level old_level;
    old_level = intr_disable ();
    thread_foreach_wait(&thread_wake_routine_helper,NULL);
    intr_set_level (old_level);
}

void
thread_wake_routine_helper (struct thread * t, void * aux)
{
    if (timer_ticks () > t->wakeup_time) {
        enum intr_level old_level;
        old_level = intr_disable ();
        list_remove (&(t->waitelem));
        intr_set_level (old_level);
        sema_up(&(t->timer_semaphore));
        //remove from list
        //up the semaphore
    }
}


/* Same as thread_foreach but called on the wait_list */
void 
thread_foreach_wait (thread_action_func *func, void *aux)
{ 
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&wait_list); e != list_end (&wait_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, waitelem);
      func (t, aux);
    }
}

/* Added for sleeping */
void
thread_sleep (int64_t ticks)
{
    struct thread *cur = thread_current ();

    cur->wakeup_time = timer_ticks () + ticks;
   

    enum intr_level old_level;
    old_level = intr_disable ();
    list_insert_ordered (&wait_list,&(cur->waitelem),&wake_time_compare,NULL);
    intr_set_level (old_level);

    sema_init (&(cur->timer_semaphore),0);
    sema_down (&(cur->timer_semaphore));

}


bool
wake_time_compare ( const struct list_elem *a, const struct list_elem *b, void *aux)
{
      struct thread *ta = list_entry (a, struct thread, waitelem);
      struct thread *tb = list_entry (b, struct thread, waitelem); // waitelem????
      if (ta->wakeup_time < tb->wakeup_time) {
        return true;
      }
      return false;
}
