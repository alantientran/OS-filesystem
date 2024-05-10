#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

static int num_total = 14;
static int num_direct = 5;
static int num_indirect = 8;
static int indirect_cnt = 128;
static int num_doubleindirect = 1;


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[107];               /* Not used. */

    block_sector_t parent;
    bool dir;
    block_sector_t ptrs[14];
    uint32_t direct;
    uint32_t indirect;
    uint32_t double_indirect;
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors (off_t size)
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
  struct inode_disk data;             /* Inode content. */
  off_t read_length;
  uint32_t direct;
  off_t length;                      
  uint32_t indirect;
  uint32_t double_indirect;
  block_sector_t ptrs[14];
  bool dir;
  block_sector_t parent;
  struct lock lock;
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector (const struct inode *inode, off_t pos, 
                                      off_t length) 
{
  ASSERT(inode != NULL);

  // invalid position
  if (pos >= length) {
    return -1;
  }

  uint32_t index;
  uint32_t blocks[indirect_cnt];

  // direct 
  if (pos < num_direct * BLOCK_SECTOR_SIZE) {
    return inode->ptrs[pos / BLOCK_SECTOR_SIZE];
  }

  // indirect 
  if (pos < (num_direct + num_indirect * indirect_cnt) * BLOCK_SECTOR_SIZE) {
    pos -= num_direct * BLOCK_SECTOR_SIZE;
    index = num_direct + (pos / (indirect_cnt * BLOCK_SECTOR_SIZE));
    pos %= indirect_cnt * BLOCK_SECTOR_SIZE;

    block_read(fs_device, inode->ptrs[index], &blocks);
  } else {  // double indirect 
    // read first index block
    block_read(fs_device, inode->ptrs[num_total - 1], &blocks);

    // read second index block
    pos -= (num_direct + num_indirect * indirect_cnt) * BLOCK_SECTOR_SIZE;
    index = pos / (indirect_cnt * BLOCK_SECTOR_SIZE);
    pos %= indirect_cnt * BLOCK_SECTOR_SIZE;

    block_read(fs_device, blocks[index], &blocks);
  }

  return blocks[pos / BLOCK_SECTOR_SIZE];
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init (void) { list_init (&open_inodes); }

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create (block_sector_t sector, off_t length, bool dir)
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
      // Nathan and Cecilia driving
      disk_inode->parent = ROOT_DIR_SECTOR;
      disk_inode->dir = dir;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      
      struct inode inode;
      inode.length = 0;
      inode.direct = 0;
      inode.indirect = 0;
      inode.double_indirect = 0;

      extend_file(&inode, disk_inode->length);
      disk_inode->direct = inode.direct;
      disk_inode->indirect = inode.indirect;
      disk_inode->double_indirect = inode.double_indirect;
      memcpy(&disk_inode->ptrs, &inode.ptrs,
       num_total * sizeof(block_sector_t));

      block_write (fs_device, sector, disk_inode);
      success = true; 
      // End of Nathan and Cecilia driving
        
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *inode_open (block_sector_t sector)
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
  lock_init(&inode->lock);
  struct inode_disk inode_disk;

  // set inode data
  block_read(fs_device, inode->sector, &inode_disk);
  inode->double_indirect = inode_disk.double_indirect;
  inode->direct = inode_disk.direct;
  inode->indirect = inode_disk.indirect;
  inode->read_length = inode_disk.length;
  inode->length = inode_disk.length;
  inode->dir = inode_disk.dir;
  inode->parent = inode_disk.parent;
  memcpy(&inode->ptrs, &inode_disk.ptrs, num_total * sizeof(block_sector_t));
  return inode;
}

/* Reopens and returns INODE. */
struct inode *inode_reopen (struct inode *inode)
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

/* Closes INODE and writes it to disk. (Does it?  Check code.)
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close (struct inode *inode) 
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
        }
      // Jeanie and Alan driving
      else { // copy data to inode disk
        struct inode_disk inode_disk;
        inode_disk.length = inode->length;
        inode_disk.direct = inode->direct;
        inode_disk.indirect = inode->indirect;
        inode_disk.double_indirect = inode->double_indirect;
        inode_disk.parent = inode->parent;
        inode_disk.dir = inode->dir;
        memcpy(&inode_disk.ptrs, &inode->ptrs,
               num_total * sizeof(block_sector_t));
        block_write(fs_device, inode->sector, &inode_disk);
      }  
      // End of Jeanie and Alan driving
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size, 
                      off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = 
      byte_to_sector (inode, offset, inode->read_length);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->read_length - offset;
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

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   extend is not yet implemented.) */
off_t inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                        off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* extend if necessary */
  if(offset + size > inode_length(inode))
  {
    if(!inode->dir) 
    {
      lock_acquire(&inode->lock);
    }
    inode->length = extend_file(inode, offset + size);
    
    if (!inode->dir) {
      lock_release(&inode->lock);
    }
  }


  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector (inode, offset, 
                                                inode_length(inode));
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
      {
        /* Write full sector directly to disk. */
        block_write (fs_device, sector_idx, buffer + bytes_written);
      }
    else 
      {
        /* We need a bounce buffer. */
        if (bounce == NULL) 
          {
            bounce = malloc (BLOCK_SECTOR_SIZE);
            if (bounce == NULL)
              break;
          }

        /* If the sector contains data before or after the chunk
            we're writing, then we need to read in the sector
            first.  Otherwise we start with a sector of all zeros. */
        if (sector_ofs > 0 || chunk_size < sector_left) 
          block_read (fs_device, sector_idx, bounce);
        else
          memset (bounce, 0, BLOCK_SECTOR_SIZE);
        memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
        block_write (fs_device, sector_idx, bounce);
      }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free (bounce);
  inode->read_length = inode_length(inode);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length (const struct inode *inode) { return inode->length; }

// Alan and Jeanie driving
/* extend the file if not enough space */
off_t extend_file (struct inode *inode, off_t length)
{
  size_t extend_size = bytes_to_sectors(length) - 
                     bytes_to_sectors(inode->length);

  if (extend_size == 0) {
    return length;
  }

  static char empty[BLOCK_SECTOR_SIZE];

  extend_direct(inode, &extend_size, empty);
  extend_indirect(inode, &extend_size, empty);
  extend_double_indirect(inode, &extend_size, empty);

  return length;
}

/* extend direct pointers*/
void extend_direct (struct inode *inode, size_t *extend_size, char *empty)
{
  while (inode->direct < num_direct && *extend_size != 0) {
    free_map_allocate(1, &inode->ptrs[inode->direct]);
    block_write(fs_device, inode->ptrs[inode->direct], empty);
    inode->direct++;
    (*extend_size)--;
  }
}
//Alan and Jeanie done driving

//Nathan and Cecilia driving
/* extend indirect pointer */
void extend_indirect (struct inode *inode, size_t *extend_size, char *empty)
{
  while (num_direct + num_indirect > inode->direct && *extend_size != 0) {
    allocate_indirect(inode, inode->direct, empty, extend_size);
  }
}

/* extend the double indirect pointers*/
void extend_double_indirect (struct inode *inode, size_t *extend_size, 
                             char *empty)
{
  if (inode->direct == num_total - 1 && *extend_size != 0) {
    block_sector_t first[128];
    block_sector_t second[128];

    if (inode->double_indirect == 0 && inode->indirect == 0) {
      free_map_allocate(1, &inode->ptrs[inode->direct]);
    } else {
      block_read(fs_device, inode->ptrs[inode->direct], &first);
    }

    while (inode->indirect < indirect_cnt && *extend_size != 0) {
      allocate_indirect(inode, inode->direct, empty, extend_size);
    }

    block_write(fs_device, inode->ptrs[inode->direct], &first);
  }
}

/* grow the file's indirect pointers*/
void allocate_indirect (struct inode *inode, uint32_t index, char *empty, 
                        size_t *extend_size)
{
  block_sector_t blocks[128];

  if (inode->indirect == 0) {
    free_map_allocate(1, &inode->ptrs[index]);
  } else {
    block_read(fs_device, inode->ptrs[index], &blocks);
  }

  while (indirect_cnt > inode->indirect && *extend_size != 0) {
    free_map_allocate(1, &blocks[inode->indirect]);
    block_write(fs_device, blocks[inode->indirect], empty);
    inode->indirect++;
    (*extend_size)--;
  }

  block_write(fs_device, inode->ptrs[index], &blocks);

  if (inode->indirect == indirect_cnt) {
    inode->indirect = 0;
    inode->direct++;
  }
}
//Nathan and Cecilia done driving

//Alan driving
/* return true if a directory */
bool inode_dir (struct inode *i) {
  return i->dir;
}

/* lock inode*/
void inode_lock (struct inode *i) {
  lock_acquire(&i->lock);
}

/* unlock inode*/
void inode_unlock (struct inode *i) {
  lock_release(&i->lock);
}

/* return the number of open files */
int get_num_open (struct inode *i)
{
  return i->open_cnt;
}

/* return the parent inode */
block_sector_t inode_get_parent (struct inode *i) {
  return i->parent;
}
//Alan done driving