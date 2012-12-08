#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

// Index into sectors for an inode_disk
#define INDIRECT_IDX 123
#define DBL_INDIRECT_IDX 124

#define DIRECT_CNT 123
#define INDIRECT_CNT 1
#define DBL_INDIRECT_CNT 1
#define SECTOR_CNT (DIRECT_CNT + INDIRECT_CNT + DBL_INDIRECT_CNT)

#define PTRS_PER_SECTOR ((off_t) (BLOCK_SECTOR_SIZE / sizeof (block_sector_t)))
// How much data one inode can point to
#define INODE_SPAN ((DIRECT_CNT                                              \
                     + PTRS_PER_SECTOR * INDIRECT_CNT                        \
                     + PTRS_PER_SECTOR * PTRS_PER_SECTOR * DBL_INDIRECT_CNT) \
                    * BLOCK_SECTOR_SIZE)
static bool gd = false; 
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    enum inode_type type;
    off_t length;                       /* File size in bytes. */ 
    unsigned magic;                     /* Magic number. */
    block_sector_t sectors[SECTOR_CNT]; 
  };


static bool
extend_file (struct inode *inode, off_t length, struct inode_disk * in_inode_disk);

static void
calculate_indices (off_t sector_idx, size_t offsets[], size_t *offset_cnt);

// change later TODO
static bool 
get_data_block (struct inode *inode, off_t offset, bool allocate,
                struct inode  **data_block, int * temp_block_nr);
//                struct cache_block **data_block, int * temp_block_nr);

static void
deallocate_inode (const struct inode *inode);

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Controls access to open_inodes list. */
static struct lock open_inodes_lock;

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    struct lock lock;                   /* Protects the inode. */
//    struct inode_disk data;             /* Inode content. DEPRECATED */

    /* Denying writes. */
    struct lock deny_write_lock;        /* Protects members below. */
    struct condition no_writers_cond;   /* Signaled when no writers. */ 
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    int writer_cnt;                     /* Number of writers. */

  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  struct inode_disk * disk_inode = NULL;
  disk_inode = calloc(1,sizeof * disk_inode);
  full_read(inode->sector, disk_inode, 512, 0);
  //block_read (fs_device, inode->sector, disk_inode);
  if (pos > disk_inode->length) 
  {
    free (disk_inode);
    return -1;
  }

  uint32_t offsets[3];
  block_sector_t buf[PTRS_PER_SECTOR];
  uint32_t offset_cnt;
  calculate_indices (pos / BLOCK_SECTOR_SIZE, offsets, &offset_cnt);

  if (offset_cnt == 1) 
  {
    free (disk_inode);
    return disk_inode->sectors[offsets[0]];
  }
  else if (offset_cnt == 2)
  {
    full_read(disk_inode->sectors[INDIRECT_IDX], buf, BLOCK_SECTOR_SIZE, 0);
    //block_read (fs_device, disk_inode->sectors[INDIRECT_IDX], (void *) buf);
    free (disk_inode);
    return buf[offsets[1]];
  }
  else if (offset_cnt == 3)
  {
    full_read(disk_inode->sectors[DBL_INDIRECT_IDX], buf, BLOCK_SECTOR_SIZE, 0);
    full_read(buf[offsets[1]], buf, BLOCK_SECTOR_SIZE, 0);
    //block_read (fs_device, disk_inode->sectors[DBL_INDIRECT_IDX], (void *) buf);
    //block_read (fs_device,  buf[offsets[1]], (void *) buf);
    free (disk_inode);
    return buf[offsets[2]];
  }

  free (disk_inode);
  return -1;
}

bool
same_sector(struct inode *node1, struct inode *node2)
{
  bool output = node1->sector == node2->sector;
  return output;
}



/* Initializes the inode module. */
void
inode_init (void) 
{
  lock_init (&open_inodes_lock);
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
// NOTE: the sector was previously determined to have been free.
bool
inode_create (block_sector_t sector, off_t length, enum inode_type type)
{
  if (gd) printf(":::inode_create to sector %d length %d\n",sector,length);
//  if (gd) printf(":::inode_create to sector %d length %d\n",sector,length);
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      // aughhhh TODO BLOCK
      disk_inode->length = -1;
      disk_inode->type = type;
      disk_inode->magic = INODE_MAGIC;
      // Implicitly writes all the new blocks does not zero them yet
      if (false == (success = extend_file ( NULL, length, disk_inode)))
      {
        struct inode * an_inode = (struct inode *) malloc(sizeof(struct inode));
        an_inode->sector = sector;
        deallocate_inode (an_inode);
        free (an_inode);
        PANIC ("HOLY SHIT INODE CREATE FAILED\n");
      }
      full_write(sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
      //block_write (fs_device, sector, disk_inode);
      ASSERT (disk_inode->length == length);
      ASSERT (disk_inode->magic == INODE_MAGIC);
  if (gd)    printf("Length of new inode %p at sector %d: %d\n",disk_inode,sector,disk_inode->length);
   if (gd)   printf("Inode's first data sector at sector %d\n",disk_inode->sectors[0]);
      
  }
  //ADDED FOR SYN_READ - gotta be careful with the memory
  free(disk_inode);
  if (gd) printf("---Finish inode_create with status %d\n",success);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  if (gd) printf (":::inode_open looking for an inode in sector %d\n",sector);
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->removed = false;
  lock_init(&inode->lock);

  cond_init(&inode->no_writers_cond);
  lock_init (&inode->deny_write_lock);
  inode->deny_write_cnt = 0;
  inode->writer_cnt = 0;

  if (gd) printf("---exit inode_open (found inode!!!1\n");
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    lock_acquire (&open_inodes_lock);
    inode->open_cnt++;
    lock_release (&open_inodes_lock);
  }
  return inode;
}


/* Returns the type of INODE. */
enum inode_type
inode_get_type (const struct inode *inode) 
{
  struct inode_disk * disk_inode  = NULL;
  disk_inode = calloc(1,sizeof(*disk_inode));
  ASSERT (disk_inode != NULL);
  full_read(inode->sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
  //block_read (fs_device, inode->sector, disk_inode);
  enum inode_type type = disk_inode->type;
  free (disk_inode);
  return type;

}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  if (gd) printf (":::inode_close with inode at %d\n",inode->sector);
  /* Ignore null pointer. */
  if (inode == NULL) {
if (gd)    printf("---inode_close inode is null\n");
    return;
  }

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0) {
    if (gd)printf("removing resources\n");
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      deallocate_inode (inode);
    }

    free (inode); 
 }
 if (gd) printf("--- inode_close done\n");
}

/* Deallocates the blocks allocated for INODE. */
static void
deallocate_inode (const struct inode *inode)
{
  
  if (gd) printf(":::deallocate_inode\n");
  struct inode_disk * disk_inode = NULL;
  bool do_indirect_release = false;
  bool do_dbl_release = false;

  disk_inode = calloc(1, sizeof *disk_inode);
  full_read(inode->sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
  //block_read (fs_device, inode->sector, disk_inode);
  block_sector_t * indirect_buf = NULL;
  block_sector_t * dbl_buf = NULL;

  uint32_t nr_sectors = 1 + ((disk_inode->length -1) / BLOCK_SECTOR_SIZE);
  if (disk_inode->length == 0) nr_sectors = 0;

  uint32_t next = 0;

  while (next < nr_sectors)
  {
    // Release first DIRECT_CNT sectors
    if (next < DIRECT_CNT) 
    {
      if (gd) printf("free_map_release sector %d\n",disk_inode->sectors[next]);
      free_map_release (disk_inode->sectors[next],1);
    } 
    // Traverse ptrs in indirect table, release them
    else if (next < DIRECT_CNT + PTRS_PER_SECTOR)
    {
      do_indirect_release = true;
      if (indirect_buf == NULL)
      {
        indirect_buf = (block_sector_t *) malloc(BLOCK_SECTOR_SIZE);
        full_read(disk_inode->sectors[INDIRECT_IDX], indirect_buf, BLOCK_SECTOR_SIZE, 0);
        //block_read (fs_device, disk_inode->sectors[INDIRECT_IDX], indirect_buf);
      }
      if (gd) printf("free_map_release sector %d\n",indirect_buf[next-DIRECT_CNT]);
      free_map_release(indirect_buf[next - DIRECT_CNT],1);
      free(indirect_buf); 
    }
    else 
    {
      // Next index into an indirect table, which indirect table.
      block_sector_t rel_next = (next - DIRECT_CNT - PTRS_PER_SECTOR) % PTRS_PER_SECTOR;
      block_sector_t ind_nr = (next - DIRECT_CNT - PTRS_PER_SECTOR) / PTRS_PER_SECTOR;

      // First get the table of indrt ptrs
      if (dbl_buf == NULL) 
      {
        dbl_buf = (block_sector_t *) malloc (BLOCK_SECTOR_SIZE);
        full_read(disk_inode->sectors[DBL_INDIRECT_IDX], dbl_buf, BLOCK_SECTOR_SIZE, 0);
        //block_read (fs_device, disk_inode->sectors[DBL_INDIRECT_IDX], dbl_buf);
      }

      // Read in next indirect table when at its first entry
      if ((next - DIRECT_CNT - PTRS_PER_SECTOR) % PTRS_PER_SECTOR == 0)
      {
        // deallocate this indirect ptr table 
        free_map_release(dbl_buf[ind_nr],1);
        full_read(dbl_buf[ind_nr], indirect_buf, BLOCK_SECTOR_SIZE, 0);
        //block_read (fs_device, dbl_buf[ind_nr], indirect_buf);
      }
      free(dbl_buf); 
      free_map_release(indirect_buf[rel_next],1);
      //free(indirect_buf);
      do_dbl_release = true;
    }
    next += 1;
  }
  //if (indirect_buf != NULL) free(indirect_buf);
  //if (dbl_buf != NULL) free(dbl_buf);
  // Possibly release the 1st indirect and dbl tables
  if (do_indirect_release == true) free_map_release(disk_inode->sectors[INDIRECT_IDX],1);
  if (do_dbl_release == true){
    free_map_release (disk_inode->sectors[DBL_INDIRECT_IDX],1);
  }
  //if (indirect_buf != NULL) {
  //  free(indirect_buf);
  //}
  
  free (disk_inode);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Translates SECTOR_IDX into a sequence of block indexes in
   OFFSETS and sets *OFFSET_CNT to the number of offsets. */
// Assumes offsets has at least 3 spaces allocated to it
// Assumes error checking for sector_idx hapens before this
static void
calculate_indices (off_t sector_idx, size_t offsets[], size_t *offset_cnt)
{
  if (gd) printf(":::calculate_indices with %p %p\n",offsets,offset_cnt);
  /* Handle direct blocks. */
  if (sector_idx <  DIRECT_CNT)
  {
   
    offsets[0] = sector_idx;    
    *offset_cnt = 1;
   if (gd) printf("---calc_indcs: direct  - %d\n",offsets[0]);
    return;
  } 

  // Somewhere in the indirect block
  if (sector_idx < DIRECT_CNT + PTRS_PER_SECTOR)
  {
    offsets[0] = INDIRECT_IDX;
    offsets[1] = sector_idx - DIRECT_CNT;
    *offset_cnt = 2;
    if (gd)printf("---calc_indcs: indrct  - %d %d\n",offsets[0],offsets[1]);
    return;
  }

  offsets[0] = DBL_INDIRECT_IDX;
  // Gosh I hope this does the floor function.
  offsets[1] = ( sector_idx - DIRECT_CNT - PTRS_PER_SECTOR ) / PTRS_PER_SECTOR;
  offsets[2] = ( sector_idx - DIRECT_CNT - PTRS_PER_SECTOR ) % PTRS_PER_SECTOR;
  *offset_cnt = 3;
  if (gd)printf("---calc_indcs: dbl - %d %d %d\n",offsets[0],offsets[1],offsets[2]);
}
/* Retrieves the data block for the given byte OFFSET in INODE,
   setting *DATA_BLOCK to the block.
   Returns true if successful, false on failure.
   If ALLOCATE is false, then missing blocks will be successful
   with *DATA_BLOCk set to a null pointer.
   If ALLOCATE is true, then missing blocks will be allocated.
   The block returned will be locked, normally non-exclusively,
   but a newly allocated block will have an exclusive lock. */
// TODO FIX PROTOTYPE - WE JUST SET THE OUTPRAM TEMP_BLOCK_NR
static bool 
get_data_block (struct inode *inode, off_t offset, bool allocate,
                struct inode **data_block, int * temp_block_nr)
//                struct cache_block **data_block, int * temp_block_nr)
{
  if (gd) printf(":::get_data_block: inodesector %d, offset %d, ptr to sector outparam: %p\n",inode->sector,offset,temp_block_nr);

  size_t offsets[3];
  size_t offset_cnt;
  struct inode_disk * disk_inode = NULL; // Needed for sectors table
  disk_inode = calloc (1, sizeof (struct inode_disk));
  ASSERT (disk_inode != NULL);
  //TODO BLOCK
  
  full_read(inode->sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
  //block_read (fs_device,inode->sector,disk_inode); // Get the inode data
  ASSERT(disk_inode->magic == INODE_MAGIC);
  // If th inode is too short and we don't allocate
  // then just return true
  if (offset > disk_inode->length)
  {
    if (gd)   printf( "Offset is longer than inode!! This better be on a read or this is a bug.\n");
    printf("Offset: %d Length: %d\n",offset,disk_inode->length);
    if (false == allocate)
    {
      if (gd)printf( "...but not allocating, so exit!\n");
      free (disk_inode);
      if (data_block != NULL) *data_block = NULL;
      return true;
    }
    if (gd)printf( "But we're gonna allocate anyways!!\n");
  }
  block_sector_t * buf = calloc(PTRS_PER_SECTOR,sizeof(block_sector_t));
  ASSERT(buf != NULL);

  // For now, just call calculate indices to grab the block number until we get
  // cache working.
  calculate_indices (offset /  BLOCK_SECTOR_SIZE,offsets,&offset_cnt);
  
  if (offset_cnt == 1) 
  {
    *temp_block_nr = disk_inode->sectors[offsets[0]];
  }
  else if (offset_cnt == 2)
  {
    full_read(disk_inode->sectors[INDIRECT_IDX], buf, BLOCK_SECTOR_SIZE, 0);
    //block_read (fs_device, disk_inode->sectors[INDIRECT_IDX], (void *) buf);
    *temp_block_nr =  buf[offsets[1]];
  }
  else if (offset_cnt == 3)
  {
    // Get data in sector the dbl indirect ptr points to.
    full_read(disk_inode->sectors[DBL_INDIRECT_IDX], buf, BLOCK_SECTOR_SIZE, 0);
    //block_read (fs_device, disk_inode->sectors[DBL_INDIRECT_IDX], (void *) buf);
    // Using our 2nd offset, find the sector nr of the sector we want.
    full_read(buf[offsets[1]], buf, BLOCK_SECTOR_SIZE, 0);
    //block_read (fs_device,  buf[offsets[1]], (void *) buf);
    *temp_block_nr = buf[offsets[2]];
  }

  if (gd)printf("---get_data_block EXIT: block_nr: %d\n",*temp_block_nr);
  free (buf);
  free (disk_inode);
  return true;

}


/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  if (gd) printf("\e[1;32m:::inode_read_at : disk at %d, reading %d bytes from off %d\n\e[1;37m",inode->sector,size,offset);
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //TODO remove later
  uint8_t * sector_data = calloc(PTRS_PER_SECTOR,sizeof(block_sector_t));

  while (size > 0) 
    {
      /* Sector to read, starting byte offset within sector, sector data. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      //TODO
//      struct cache_block *block;
      struct inode * block = NULL;    

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      int *sector_nr = calloc(1, sizeof(int));
      *sector_nr = -1;
      // TODO change back
//      if (chunk_size <= 0 || !get_data_block (inode, offset, false, &block))
      if (chunk_size <= 0 || !get_data_block (inode, offset, false, NULL, sector_nr))
        break;

      // If the read fails, still set this to 0??
      // holy fucking shit BUG god
//      if (block == NULL) 
      if (*sector_nr == -1) {
        memset (buffer + bytes_read, 0, chunk_size);
      }
      else 
        {
          // change to block read
          // TODO
//          const uint8_t *sector_data = cache_read (block);

if (gd)    printf ("Reading %d bytes sector off %d into buffer off %d\n",chunk_size,sector_ofs,bytes_read);
          full_read(*sector_nr, sector_data, BLOCK_SECTOR_SIZE, 0);
          //block_read (fs_device, *sector_nr, sector_data);
          memcpy (buffer + bytes_read, sector_data + sector_ofs, chunk_size);
        //  cache_unlock (block);
        }
     
      free(sector_nr);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
 //need to watch for running out of memory!!!
 free(sector_data);
 if (gd) printf("---inode_read_at EXIT (READ %d BYTES\n",bytes_read);
  return bytes_read;
}

/* Extends INODE to be at least LENGTH bytes long, modifying hte underlinyg
 * inode_disk in the process. Or jjust does this to the inode_disk.
 * If the latter, you had better write it to disk outside of this functio
 * call.*/
// Returns FALSE if some allocation fails, TRUE on success 
static bool
extend_file (struct inode *inode, off_t length, struct inode_disk * in_inode_disk) 
{
  // Bad length arg
  
  if (gd) printf ("\e[1;31m:::extend_file %p %d %p\n\e[1;37m",inode,length,in_inode_disk);
  if (length > INODE_SPAN || length < 0) {
   if (gd) printf("BAD LENGTH EXTEND FILE\n");
    return false;
  }

  ASSERT (inode != NULL || in_inode_disk != NULL);

  bool id_alloc = false;
  struct inode_disk * inode_disk = NULL;
  if (in_inode_disk == NULL) {
    inode_disk = calloc (1, sizeof (struct inode_disk));
    id_alloc = true;
    if (inode_disk == NULL) {
      if (gd)printf("CALLOC FAILED EXTEND_FILE\n");
      return  false;
    }
    full_read(inode->sector, inode_disk, BLOCK_SECTOR_SIZE, 0);
    //block_read(fs_device,inode->sector,inode_disk);
  } else {
    inode_disk = in_inode_disk;
  }

  // Index of the last new sector to create, index of the last 
  // allocated sector of current inode
  block_sector_t last_new_sector = ((length - 1) / BLOCK_SECTOR_SIZE);
  block_sector_t last_old_sector = ((inode_disk->length - 1) / BLOCK_SECTOR_SIZE);
  if (inode_disk->length != -1 && last_new_sector <= last_old_sector) {
      if (gd)printf("\e[1;31m----extend_file:EXITING ( cur_length: %d, offset: %d \n\e[1;37m",inode_disk->length,length);
    if (id_alloc) free (inode_disk);
    return true;
  }

  // Otherwise need to allocate new sectors
  block_sector_t next_sector_idx = last_old_sector + 1;
  if (inode_disk->length == -1) next_sector_idx = 0; // EDGE CASE!!
  block_sector_t sector_nr;
  block_sector_t * buf = (block_sector_t *) malloc(BLOCK_SECTOR_SIZE);
  memset(buf, 0, BLOCK_SECTOR_SIZE);
  size_t offsets[3];
  size_t offset_cnt;
  while (next_sector_idx <= last_new_sector) {
    if (gd)printf("Adding new sector INDEX %d to disk inode, byte off %d",next_sector_idx,512*next_sector_idx);
    // TODO update args when get cache
    calculate_indices ((size_t) next_sector_idx, offsets, &offset_cnt);

    if (false == free_map_allocate (1, &sector_nr)) {
      if (gd)  printf("file_extend: free_map_allocate failed\n");
      free(buf);
      if (id_alloc) free(inode_disk);
      return false;
    }
    // Zero out the new sector.
    memset(buf, 0, BLOCK_SECTOR_SIZE);
    full_write(sector_nr, buf, BLOCK_SECTOR_SIZE, 0);
    //block_write (fs_device, sector_nr, buf);

    block_sector_t new_sector;
    // Find the right place to put the idx -> sector_nr mapping.
    if (next_sector_idx < DIRECT_CNT) {
     if (gd) printf("DIRECT SECTOR!!! %d\n",sector_nr);
      inode_disk->sectors[next_sector_idx] = sector_nr; 
    } else if (next_sector_idx < DIRECT_CNT + PTRS_PER_SECTOR) {
      // TODO
      
      // Allocate indirect-table.
      if (next_sector_idx == DIRECT_CNT) {
        if (gd)printf("**** MAKING NEW SPOT FOR INDRECT PAGE\n");
        if (false == free_map_allocate (1, &new_sector)) {
          if (gd)printf("FREE MAP ALLOCATE FAILED IN EXTEND FILE\n");
          if (id_alloc) free(inode_disk);
          free(buf);
          return false;
        }
        inode_disk->sectors[INDIRECT_IDX] = new_sector;
        if (gd)printf("****  DONE NEW SPOT FOR INDRECT PAGE\n");
      }
      if (gd)printf("INDIRECT SECTOR!!! %d, into %d, indtab at %d\n",sector_nr,offsets[1],inode_disk->sectors[INDIRECT_IDX]);
      full_read(inode_disk->sectors[INDIRECT_IDX], buf, BLOCK_SECTOR_SIZE, 0);
      //block_read(fs_device,inode_disk->sectors[INDIRECT_IDX],(void *) buf);
      buf[offsets[1]] = sector_nr;
      full_write(inode_disk->sectors[INDIRECT_IDX], buf, BLOCK_SECTOR_SIZE, 0);
      //block_write(fs_device,inode_disk->sectors[INDIRECT_IDX],(void *) buf);
    } else {
      if (gd)printf("DBL SECTOR!!! %d\n",sector_nr);

      // Allocate the dbl-indirect-table
      if (next_sector_idx == DIRECT_CNT + PTRS_PER_SECTOR) {
        if (false == free_map_allocate (1, &new_sector)) {
          free(buf);
          if (id_alloc) free (inode_disk);
          return false;
        }
        inode_disk->sectors[DBL_INDIRECT_IDX] = new_sector;
      }

      // Allocate a new indirect table
      bool new_indirect = false;
      if (offsets[2] == 0) {
        if (false == free_map_allocate (1, &new_sector)) {
          free(buf);
          if (id_alloc) free (inode_disk);
          return false;
        }
        new_indirect = true;
      }
      
      // Get dbl-indirect-table
      full_read(inode_disk->sectors[DBL_INDIRECT_IDX], buf, BLOCK_SECTOR_SIZE, 0);
      //block_read (fs_device,inode_disk->sectors[DBL_INDIRECT_IDX], (void *) buf);
      // Maybe update the dbl-indirect-table with a new ptr to an indirect table
      if (new_indirect == true) {
        buf[offsets[1]] = new_sector;
        full_write(inode_disk->sectors[DBL_INDIRECT_IDX], buf, BLOCK_SECTOR_SIZE, 0);
        //block_write (fs_device, inode_disk->sectors[DBL_INDIRECT_IDX],(void *) buf);
      }
      // Get pointer to indirect-table
      block_sector_t dbl_indirect_first = (block_sector_t) buf[offsets[1]];
      // Get indirect-table
      full_read(dbl_indirect_first, buf, BLOCK_SECTOR_SIZE, 0);
      //block_read (fs_device,dbl_indirect_first, (void *) buf);
      // Add sector entry to indirect-table
      buf[offsets[2]] = sector_nr; 
      full_write(dbl_indirect_first, buf, BLOCK_SECTOR_SIZE, 0);
      //block_write (fs_device, dbl_indirect_first, (void *) buf);
    }

    next_sector_idx += 1;
  }

  // Write to disk ONLY if we were passed an inode struct (rather than inode_disk)
  inode_disk->length = length;

  if (gd) printf ("\e[1;31m---extend_file: success - new len %d\n\e[1;37m",length);
  if (in_inode_disk == NULL) {
    full_write(inode->sector, inode_disk, BLOCK_SECTOR_SIZE, 0);
    //block_write (fs_device, inode->sector, inode_disk);
    free (inode_disk);
  }
  free(buf);
  return true;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
//  if (gd) printf ("\e[1;34m:::inode_write_at - writing %d bytes at off %d to disk inode in sector %d\n\e[1;37m",size,offset,inode->sector);
  if (gd) printf ("\e[1;34m:::inode_write_at - writing %d bytes at off %d to disk inode (metadata at) %d\n\e[1;37m",size,offset,inode->sector);
  const uint8_t *buffer = buffer_;
//  printf("%d %d %d %d\n",buffer[0],buffer[1],buffer[2],buffer[3]);
  off_t bytes_written = 0;

  /* Don't write if writes are denied. */
  lock_acquire (&inode->deny_write_lock);
  if (inode->deny_write_cnt) 
    {
      lock_release (&inode->deny_write_lock);
      return 0;
    }
  inode->writer_cnt++;
  lock_release (&inode->deny_write_lock);

  // We might need to allocate new sectors for the inode.
  extend_file (inode, offset + size, NULL);

  // We need to get a copy of the sector to change a small part of it possibly
  uint8_t *sector_data = malloc(BLOCK_SECTOR_SIZE);
  while (size > 0) 
    {
      int * temp_block_nr = calloc (1, sizeof(int));
      /* Sector to write, starting byte offset within sector, sector data. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
//      struct cache_block *block;

      /* Bytes to max inode size, bytes left in sector, lesser of the two. */
      off_t inode_left = INODE_SPAN - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;

      // Chunk_size will be less than 0 if we run out of space in INODE 
      // so we don't have to error check in get_data_block or calculate_indices

      if (chunk_size <= 0 || !get_data_block (inode, offset, true, NULL,temp_block_nr))
        break;
       
      if (gd)printf("To sector %d ",temp_block_nr[0]);
      if (gd) printf("Write %d bytes from buffer off %d into sector off %d\n",chunk_size,bytes_written,sector_ofs);
      // Get copy of sector
      full_read(temp_block_nr[0], sector_data, BLOCK_SECTOR_SIZE, 0);
      //block_read (fs_device, temp_block_nr[0], sector_data);
      memcpy (sector_data + sector_ofs, buffer + bytes_written, chunk_size);
      full_write(temp_block_nr[0], sector_data, BLOCK_SECTOR_SIZE, 0);
      //block_write (fs_device, temp_block_nr[0], sector_data);
//    sector_data = cache_read (block);
//    cache_dirty (block);
//    cache_unlock (block);

      free (temp_block_nr);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }



  lock_acquire (&inode->deny_write_lock);
  if (--inode->writer_cnt == 0)
    cond_signal (&inode->no_writers_cond, &inode->deny_write_lock);
  lock_release (&inode->deny_write_lock);

  if (gd) {
     printf("---We wrote %d bytes\n",bytes_written);
  } 
  free(sector_data);
  return bytes_written;
}
/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk * disk_inode = NULL;
  disk_inode = calloc(1, sizeof *disk_inode);
  ASSERT (disk_inode != NULL);
  // TODO!
  full_read(inode->sector, disk_inode, BLOCK_SECTOR_SIZE, 0);
  //block_read (fs_device, inode->sector, disk_inode);
  off_t length = disk_inode->length;
  free(disk_inode);
  return length;
}


/* Returns the number of openers. */
int
inode_open_cnt (const struct inode *inode) 
{
  int open_cnt;
  
  lock_acquire (&open_inodes_lock);
  open_cnt = inode->open_cnt;
  lock_release (&open_inodes_lock);

  return open_cnt;
}

/* Locks INODE. */
void
inode_lock (struct inode *inode) 
{
  lock_acquire (&inode->lock);
}

/* Releases INODE's lock. */
void
inode_unlock (struct inode *inode) 
{
  lock_release (&inode->lock);
}
