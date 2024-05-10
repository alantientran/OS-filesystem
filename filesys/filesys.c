#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

// Jeanie driving
struct dir *get_dir (const char *path);
char *get_file_name (const char *path_name);
// End of Jeanie driving

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done (void) { free_map_close (); }

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  // Alan driving
  struct dir *dir = get_dir(name);
  char *file_name = get_file_name(name);

  // Check for current and parent directories
  if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
    free(file_name);
    dir_close(dir);
    return false;
  }

  // Attempt to create file or directory
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL && free_map_allocate(1, &inode_sector) &&
                  inode_create(inode_sector, initial_size, is_dir) &&
                  dir_add(dir, file_name, inode_sector));

  free(file_name);
  dir_close(dir);

  if (!success && inode_sector != 0) 
    free_map_release(inode_sector, 1);

  return success;
  // End of Alan driving
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *filesys_open (const char *name)
{
  // Alan and Cecilia driving
  if (strlen(name) == 0) 
    return NULL;

  char *file_name = get_file_name(name);
  struct dir *dir = get_dir(name);

  // can't open directory
  if (!dir) {
    free(file_name);
    return NULL;
  }

  struct inode *inode = NULL;

  if (strcmp(file_name, "..") == 0) {  // parent directory
    inode = dir_parent(dir);
  } else if ((is_root(dir) && strlen(file_name) == 0) || 
             (strcmp(file_name, ".") == 0)) {  // directory or curr
    free(file_name);
    return (struct file *) dir;
  } else {  // regular directory lookup
    dir_lookup(dir, file_name, &inode);
  }

  free(file_name);
  dir_close(dir);

  if (!inode) {
    return NULL;
  }

  if (inode_dir(inode)) {
    return (struct file *) dir_open(inode);
  }

  return file_open (inode);
  // End of Alan and Cecilia driving
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove (const char *name)
{
  // Cecilia driving
  struct dir *dir = get_dir(name);
  char *file_name = get_file_name(name);
  bool success = dir != NULL && dir_remove (dir, file_name);
  dir_close (dir); 
  free(file_name);

  return success;
  // End of Cecilia driving
}

/* Formats the file system. */
static void do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

// Cecilia and Jeanie driving
/* Retrieves and returns the name of a file or directory from a given path */
char *get_file_name (const char *path_name)
{
  int length = strlen(path_name);
  const char *end = path_name + length;

  // Find last '/'
  const char *last_slash = path_name;
  for (const char *ptr = path_name; ptr < end; ptr++) {
    if (*ptr == '/') {
      last_slash = ptr + 1;
    }
  }

  // Allocate mem for length of substring after last slash
  int name_length = length - (last_slash - path_name);
  char *name = malloc(name_length + 1);

  // Copy substring
  const char *src = last_slash;
  char* dest = name;
  while (*src != '\0') {
    *dest = *src;  // Copy curr char from src to dest
    src++;  // Move to next char in src
    dest++;  // Move to next pos in dest
  }
  *dest = '\0';

  return name;
}
// End of Cecilia and Jeanie driving

// Nathan and Alan driving
/* Gets the directory from a given path */
struct dir *get_dir (const char *path_name)
{
  char path[strlen(path_name) + 1];
  memcpy(path, path_name, strlen(path_name) + 1);

  struct dir *dir;

  // Check if current thread has directory set or if the path is absolute
  if (!thread_current()->dir || path[0] == '/') {
    dir = dir_open_root();
  } else {
    dir = dir_reopen(thread_current()->dir);
  }
  
  // Tokenize path
  char *token;
  char *cur;
  char *prev = strtok_r(path, "/", &token);
  while (cur = strtok_r(NULL, "/", &token)) {
    struct inode *inode;

    if (strcmp(prev, "..") == 0) {  // parent directory
      inode = dir_parent(dir);
    } else if (!dir_lookup(dir, prev, &inode)) {  // dir doesn't exist
      return NULL;
    }

    if (!inode) {
      return NULL;
    }
    
    if (inode_dir(inode)) {
      dir_close(dir);
      dir = dir_open(inode);
    } else {
      inode_close(inode);
      break;
    }

    prev = cur;
  }

  return dir;
}
