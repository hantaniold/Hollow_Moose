#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "filesys/free-map.h"
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
static bool gd = true;
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
  block_read (fs_device, inode->sector, disk_inode);
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
    block_read (fs_device, disk_inode->sectors[INDIRECT_IDX], (void *) buf);
    free (disk_inode);
    return buf[offsets[1]];
  }
  else if (offset_cnt == 3)
  {
    block_read (fs_device, disk_inode->sectors[DBL_INDIRECT_IDX], (void *) buf);
    block_read (fs_device,  buf[offsets[1]], (void *) buf);
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
  if (gd) printf("inode_create\n");
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
      disk_inode->length = length;
      disk_inode->type = type;
      disk_inode->magic = INODE_MAGIC;
      block_write (fs_device, sector, disk_inode);
      // Implicitly writes all the new blocks does not zero them yet
      if (false == (success = extend_file ( NULL, length, disk_inode)))
      {
        struct inode * an_inode = (struct inode *) malloc(sizeof(struct inode));
        an_inode->sector = sector;
        deallocate_inode (an_inode);
        free (an_inode);
        PANIC ("HOLY SHIT INODE CREATE FAILED\n");
      }
      
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  if (gd) printf ("In inode_open\n");
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

  lock_init (&inode->deny_write_lock);
  inode->deny_write_cnt = 0;
  inode->writer_cnt = 0;

  // do i need this?!?!
//  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    lock_acquire (&open_inodes_lock);
    inode->open_cnt++;
    lock_release (&open_inodes_lock);
  return inode;
}


/* Returns the type of INODE. */
enum inode_type
inode_get_type (const struct inode *inode) 
{
  struct inode_disk * disk_inode  = NULL;
  disk_inode = calloc(1,sizeof *disk_inode);
  ASSERT (disk_inode != NULL);
  block_read (fs_device, inode->sector, disk_inode);
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
  if (gd) printf ("In inode_close\n");
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          deallocate_inode (inode);
        }

      free (inode); 
    }
}

/* Deallocates the blocks allocated for INODE. */
static void
deallocate_inode (const struct inode *inode)
{
  if (gd) printf("in deallocate_inode\n");
  struct inode_disk * disk_inode = NULL;
  bool do_indirect_release = false;
  bool do_dbl_release = false;

  disk_inode = calloc(1, sizeof *disk_inode);
  block_read (fs_device, inode->sector, disk_inode);
  block_sector_t * indirect_buf = NULL;
  block_sector_t * dbl_buf = NULL;

  uint32_t nr_sectors = 1 + ((disk_inode->length -1) / BLOCK_SECTOR_SIZE);

  uint32_t next = 0;

  while (next < nr_sectors)
  {
    // Release first DIRECT_CNT sectors
    if (next < DIRECT_CNT) 
    {
      free_map_release (disk_inode->sectors[next],1);
    } 
    // Traverse ptrs in indirect table, release them
    else if (next < DIRECT_CNT + PTRS_PER_SECTOR)
    {
      do_indirect_release = true;
      if (indirect_buf == NULL)
      {
        indirect_buf = (block_sector_t *) malloc(BLOCK_SECTOR_SIZE);
        block_read (fs_device, disk_inode->sectors[INDIRECT_IDX], indirect_buf);
      }
      free_map_release(indirect_buf[next - DIRECT_CNT],1);
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
        block_read (fs_device, disk_inode->sectors[DBL_INDIRECT_IDX], dbl_buf);
      }

      // Read in next indirect table when at its first entry
      if ((next - DIRECT_CNT - PTRS_PER_SECTOR) % PTRS_PER_SECTOR == 0)
      {
        // deallocate this indirect ptr table 
        free_map_release(dbl_buf[ind_nr],1);
        block_read (fs_device, dbl_buf[ind_nr], indirect_buf);
      }

      free_map_release(indirect_buf[rel_next],1);

      do_dbl_release = true;
    }
    next += 1;
  }

  // Possibly release the 1st indirect and dbl tables
  if (do_indirect_release == true) free_map_release(disk_inode->sectors[INDIRECT_IDX],1);
  if (do_dbl_release == true) free_map_release (disk_inode->sectors[DBL_INDIRECT_IDX],1);
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
  if (gd) printf("in calculate_indices\n");
  /* Handle direct blocks. */
  if (sector_idx <  DIRECT_CNT)
  {
    offsets[0] = sector_idx;    
    *offset_cnt = 1;
    return;
  } 

  // Somewhere in the indirect block
  if (sector_idx < DIRECT_CNT + PTRS_PER_SECTOR)
  {
    offsets[0] = INDIRECT_IDX;
    offsets[1] = sector_idx - DIRECT_CNT;
    *offset_cnt = 2;
    return;
  }

  offsets[0] = DBL_INDIRECT_IDX;
  // Gosh I hope this does the floor function.
  offsets[1] = ( sector_idx - DIRECT_CNT - PTRS_PER_SECTOR ) / PTRS_PER_SECTOR;
  offsets[2] = ( sector_idx - DIRECT_CNT - PTRS_PER_SECTOR ) % PTRS_PER_SECTOR;
  *offset_cnt = 3;
}
/* Retrieves the data block for the given byte OFFSET in INODE,
   setting *DATA_BLOCK to the block.
   Returns true if successful, false on failure.
   If ALLOCATE is false, then missing blocks will be successful
   with *DATA_BLOCk set to a null pointer.
   If ALLOCATE is true, then missing blocks will be allocated.
   The block returned will be locked, normally non-exclusively,
   but a newly allocated block will have an exclusive lock. */
// TODO FIX PROTOTYPE
static bool 
get_data_block (struct inode *inode, off_t offset, bool allocate,
                struct inode **data_block, int * temp_block_nr)
//                struct cache_block **data_block, int * temp_block_nr)
{
  if (gd) printf("in get_data_block\n");
  allocate = allocate;
  data_block = data_block;

  size_t offsets[3];
  size_t offset_cnt;
  struct inode_disk * disk_inode = NULL; // Needed for sectors table
  disk_inode = calloc (1, sizeof *disk_inode);
  ASSERT (disk_inode != NULL);
  //TODO BLOCK
  if (offset > disk_inode->length)
  {
    if (false == allocate)
    {
      return true;
    }
    else 
    {
      return false;
    }
  }
  block_read (fs_device,inode->sector,&disk_inode); // Get the inode data
  block_sector_t buf[PTRS_PER_SECTOR];

  // For now, just call calculate indices to grab the block number until we get
  // cache working.
  calculate_indices (offset /  BLOCK_SECTOR_SIZE,offsets,&offset_cnt);
  
  if (offset_cnt == 1) 
  {
    *temp_block_nr = disk_inode->sectors[offsets[0]];
  }
  else if (offset_cnt == 2)
  {
    block_read (fs_device, disk_inode->sectors[INDIRECT_IDX], (void *) buf);
    *temp_block_nr =  buf[offsets[1]];
  }
  else if (offset_cnt == 3)
  {
    // Get data in sector the dbl indirect ptr points to.
    block_read (fs_device, disk_inode->sectors[DBL_INDIRECT_IDX], (void *) buf);
    // Using our 2nd offset, find the sector nr of the sector we want.
    block_read (fs_device,  buf[offsets[1]], (void *) buf);
    *temp_block_nr = buf[offsets[2]];
  }

  free (disk_inode);
  return true;

}


/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  if (gd) printf("in inode_read_at\n");
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //TODO remove later
  uint8_t * sector_data[BLOCK_SECTOR_SIZE];

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
      int sector_nr;
      // TODO change back
//      if (chunk_size <= 0 || !get_data_block (inode, offset, false, &block))
      if (chunk_size <= 0 || !get_data_block (inode, offset, false, NULL, &sector_nr))
        break;

      // If the read fails, still set this to 0??
      if (block == NULL) 
        memset (buffer + bytes_read, 0, chunk_size);
      else 
        {
          // change to block read
          // TODO
//          const uint8_t *sector_data = cache_read (block);
          block_read (fs_device, sector_nr, sector_data);
          memcpy (buffer + bytes_read, sector_data + sector_ofs, chunk_size);
        //  cache_unlock (block);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Extends INODE to be at least LENGTH bytes long. */
// Returns FALSE if some allocation fails, TRUE on success 
static bool
extend_file (struct inode *inode, off_t length, struct inode_disk * in_inode_disk) 
{
  // Bad length arg
  
  if (gd) printf (" IN EXTEND FILE!!! %p %d %p\n",inode,length,in_inode_disk);
  if (length > INODE_SPAN || length < 0) 
  {
    return false;
  }

  ASSERT (inode != NULL || in_inode_disk != NULL);

  struct inode_disk * inode_disk = NULL;
  if (in_inode_disk == NULL) 
  {
    inode_disk = calloc (1, sizeof *inode_disk);
    if (inode_disk == NULL) return  false;
    block_read(fs_device,inode->sector,inode_disk);
  } 
  else 
  {
    inode_disk = in_inode_disk;
  }



  // Index of the last new sector to create, index of the last 
  // allocated sector of current inode
  block_sector_t last_new_sector = ((length - 1) / BLOCK_SECTOR_SIZE);
  block_sector_t last_old_sector = ((inode_disk->length - 1) / BLOCK_SECTOR_SIZE);
  if (last_new_sector <= last_old_sector) 
  {
    if (inode != NULL) free (inode_disk);
    return true;
  }

  // Otherwise need to allocate new sectors
  // TODO maybe zero out each block???
  block_sector_t next_sector_idx = last_old_sector + 1;
  block_sector_t sector_nr;
  block_sector_t * buf = (block_sector_t *) malloc(BLOCK_SECTOR_SIZE);
  size_t offsets[3];
  size_t offset_cnt;
  while (next_sector_idx <= last_new_sector)
  {
    // TODO update args when get cache
    calculate_indices ((size_t) next_sector_idx, offsets, &offset_cnt);
    if (false == free_map_allocate (1, &sector_nr)) return false;

    // Find the right place to put the idx -> sector_nr mapping.
    if (next_sector_idx < DIRECT_CNT) 
    {
      inode_disk->sectors[next_sector_idx] = sector_nr; 
    } 
    else if (next_sector_idx < DIRECT_CNT + PTRS_PER_SECTOR)
    {
      // TODO
      
      // Need to init indirect ptr
      if (next_sector_idx == DIRECT_CNT) 
      {
        block_sector_t new_sector;
        if (false == free_map_allocate (1, &new_sector)) 
        {
          if (inode != NULL) free (inode_disk);
          return false;
        }
        inode_disk->sectors[INDIRECT_IDX] = new_sector;
      }
      block_read(fs_device,inode_disk->sectors[INDIRECT_IDX],(void *) buf);
      buf[offsets[1]] = sector_nr;
      block_write(fs_device,inode_disk->sectors[INDIRECT_IDX],(void *) buf);
    } 
    else 
    {
      // TODO
      // NEED TO CHECK if need to make new blks
      // FUCK!
      
      // Get table of ptrs pointed to by dbl_indirect
      block_read (fs_device,inode_disk->sectors[DBL_INDIRECT_IDX], (void *) buf);
      block_sector_t dbl_indirect_first = (block_sector_t) buf[offsets[1]];
      block_read (fs_device,dbl_indirect_first, (void *) buf);
      // Overwrite this table of pointers
      buf[offsets[2]] = sector_nr; 
      block_write (fs_device, dbl_indirect_first, (void *) buf);
    }

    inode_disk->length = length;
    // TODO DO I NEED TO WRITE inode_disk IN ALL CASES??
    if (inode != NULL) {
      block_write (fs_device, inode->sector, inode_disk);
      free (inode_disk);
    }

    next_sector_idx += 1;
  }
  return true;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  if (gd) printf ("in inode_write_at\n");
  const uint8_t *buffer = buffer_;
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
  extend_file (inode, offset, NULL);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector, sector data. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
//      struct cache_block *block;
//      uint8_t *sector_data;

      /* Bytes to max inode size, bytes left in sector, lesser of the two. */
      off_t inode_left = INODE_SPAN - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;

      // Chunk_size will be less than 0 if we run out of space in INODE 
      // so we don't have to error check in get_data_block or calculate_indices
      int temp_block_nr;
      //if (chunk_size <= 0 || !get_data_block (inode, offset, true, &block,&temp_block_nr))
      if (chunk_size <= 0 || !get_data_block (inode, offset, true, NULL,&temp_block_nr))
        break;
       
      block_write (fs_device, temp_block_nr, buffer + bytes_written);
//    sector_data = cache_read (block);
//    memcpy (sector_data + sector_ofs, buffer + bytes_written, chunk_size);
//    cache_dirty (block);
//    cache_unlock (block);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }


  lock_acquire (&inode->deny_write_lock);
  if (--inode->writer_cnt == 0)
    cond_signal (&inode->no_writers_cond, &inode->deny_write_lock);
  lock_release (&inode->deny_write_lock);

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
  if (gd) printf ("in inode_length\n");
  struct inode_disk * disk_inode = NULL;
  disk_inode = calloc(1, sizeof *disk_inode);
  ASSERT (disk_inode != NULL);
  // TODO!
  block_read (fs_device, inode->sector, disk_inode);
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
