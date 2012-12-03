#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
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
static bool
extend_file (struct inode *inode, off_t length);

static void
calculate_indices (off_t sector_idx, size_t offsets[], size_t *offset_cnt);

static bool 
get_data_block (struct inode *inode, off_t offset, bool allocate,
                struct cache_block **data_block, int * temp_block_nr);


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
//    enum inode_type type;
    off_t length;                       /* File size in bytes. */ // DEPRECATED
    unsigned magic;                     /* Magic number. */
    block_sector_t sectors[SECTOR_CNT]; 
  };

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
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. DEPRECATED */

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
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

bool
same_sector(struct inode *node1, struct inode *node2)
{
  bool output = node1->sector == node2->sector;
  return output;
}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
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
  inode->deny_write_cnt = 0;
  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
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
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
    }
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
static bool 
get_data_block (struct inode *inode, off_t offset, bool allocate,
                struct cache_block **data_block, int * temp_block_nr)
{

  size_t offsets[3];
  size_t offset_cnt;
  struct inode_disk * inode_disk = inode->data; // Needed for sectors table
  block_sector_t buf[PTRS_PER_SECTOR];
  block_sector_t next_sector; // dbl-indirect ptr

  // For now, just call calculate indices to grab the block number until we get
  // cache working.
  calculate_indices (offset /  BLOCK_SECTOR_SIZE,offsets,&offset_cnt);
  
  if (offset_cnt == 1) 
  {
    *temp_block_nr = inode_disk->sectors[offsets[0]];
  }
  else if (offset_cnt == 2)
  {
    block_read (fs_device, inode_disk->sectors[INDIRECT_IDX], (void *) buf);
    *temp_block_nr =  buf[offsets[1]];
  }
  else if (offset_cnt == 3)
  {
    // Get data in sector the dbl indirect ptr points to.
    block_read (fs_device, inode_disk->sectors[DBL_INDIRECT_IDX], (void *) buf);
    // Using our 2nd offset, find the sector nr of the sector we want.
    block_read (fs_device,  buf[offsets[1]], (void *) buf);
    *temp_block_nr = buf[offsets[2]];
  }

  return true;

}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Extends INODE to be at least LENGTH bytes long. */
// Returns FALSE if some allocation fails, TRUE on success 
static bool
extend_file (struct inode *inode, off_t length) 
{
  // Bad length arg
  if (length > INODE_SPAN || length < 0) 
  {
    return;
  }

  struct inode_disk inode_disk;
  block_read(fs_device,inode->sector,&inode_disk);

  // Index of the last new sector to create, index of the last 
  // allocated sector of current inode
  block_t last_new_sector = ((length - 1) / BLOCK_SECTOR_SIZE);
  block_t last_old_sector = ((inode_disk->length - 1) / BLOCK_SECTOR_SIZE);
  if (last_new_sector <= last_old_sector) 
  {
    return;
  }

  // Otherwise need to allocate new sectors
  block_t next_sector_idx = last_old_sector + 1;
  block_t sector_nr;
  block_t buf[PTRS_PER_SECTOR];
  size_t offsets[3];
  size_t offset_cnt;
  while (next_sector_idx <= last_new_sector)
  {
    // TODO update args when get cache
    calculate_indices ((size_t) next_sector_idx, offsets, size_t &offset_cnt);
    if (false == free_map_allocate (1, &sector_nr)) return false;

    // Find the right place to put the idx -> sector_nr mapping.
    if (next_sector_idx < DIRECT_CNT) 
    {
      inode_disk->sectors[next_sector_idx] = sector_nr; 
    } 
    else if (next_sector_idx < DIRECT_CNT + PTRS_PER_SECTOR)
    {
      // TODO
      block_read(fs_device,inode_disk->sectors[INDIRECT_IDX],(void *) buf);
      buf[offsets[1]] = sector_nr;
      block_write(fs_device,inode_disk->sectors[INDIRECT_IDX],(void *) buf);
    } 
    else 
    {
      // TODO
      // Get table of ptrs pointed to by dbl_indirect
      block_read (fs_device,inode_disk->sectors[DBL_INDIRECT_IDX], (void *) buf);
      block_t dbl_indirect_first = (block_t) buf[offsets[1]];
      block_read (fs_device,dbl_indirect_first, (void *) buf);
      // Overwrite this table of pointers
      buf[offsets[2]] = sector_nr; 
      block_write (fs_device, dbl_indirect_first, (void *) buf);
    }

    next_sector_idx += 1;
  }
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
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
  extend_file (inode, offset);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector, sector data. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
//      struct cache_block *block;
      uint8_t *sector_data;

      /* Bytes to max inode size, bytes left in sector, lesser of the two. */
      off_t inode_left = INODE_SPAN - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;

      // Chunk_size will be less than 0 if we run out of space in INODE 
      // so we don't have to error check in get_data_block or calculate_indices
      int temp_block_nr;
      if (chunk_size <= 0 || !get_data_block (inode, offset, true, &block,&temp_block_nr))
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
  return inode->data.length;
}
