#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "lib/user/syscall.h"

// Cecilia driving
struct semaphore file_access;
// End of Cecilia driving

static void syscall_handler (struct intr_frame *);

void syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  sema_init(&file_access, 1);
}

// Cecilia driving
/* Checks for bad pointers */
void valid_ptr (void *ptr) {
   char *cpy = ((char*) ptr);

   // checks each byte
   for (int i = 0; i < 4; i++) { 
      if (!cpy || is_kernel_vaddr(cpy) || 
         !pagedir_get_page(thread_current()->pagedir, (int*)cpy)) {
         exit(-1);
      } 
      cpy++;
   }
}
// End of Cecilia driving

static void syscall_handler (struct intr_frame *f)
{
   // Nathan driving
   char *esp = f->esp;
   valid_ptr(esp);
   
   int *cpyEsp = f->esp;
   
   switch (*cpyEsp) {
      case SYS_HALT: 
         halt();
         break;
      case SYS_EXIT: 
         valid_ptr(cpyEsp + 1);
         exit(*(cpyEsp + 1));
         break;
      case SYS_EXEC:
         valid_ptr(cpyEsp + 1);
         valid_ptr(*(cpyEsp + 1));
         f->eax = exec(*(cpyEsp + 1));
         break;
      case SYS_WAIT:
         valid_ptr(cpyEsp + 1);
         f->eax = wait(*(cpyEsp + 1));
         break;
      case SYS_CREATE:
         valid_ptr(cpyEsp + 1);
         valid_ptr(*(cpyEsp + 1));
         valid_ptr(cpyEsp + 2);
         f->eax = create(*(cpyEsp + 1), *(cpyEsp + 2));
         break;
      case SYS_REMOVE:
         valid_ptr(cpyEsp + 1);
         valid_ptr(*(cpyEsp + 1));
         f->eax = remove(*(cpyEsp + 1));
         break;
      case SYS_OPEN:
         valid_ptr(cpyEsp + 1);
         valid_ptr(*(cpyEsp + 1));
         f->eax = open(*(cpyEsp + 1));
         break;
      case SYS_FILESIZE:
         valid_ptr(cpyEsp + 1);
         f->eax = filesize(*(cpyEsp + 1));
         break;
      case SYS_READ:
         valid_ptr(cpyEsp + 1);
         valid_ptr(*(cpyEsp + 2));
         valid_ptr(cpyEsp + 3);
         f->eax = read(*(cpyEsp + 1), *(cpyEsp + 2), *(cpyEsp + 3));
         break;
      case SYS_WRITE:
         valid_ptr(cpyEsp + 1);
         valid_ptr(*(cpyEsp + 2));
         valid_ptr(cpyEsp + 3);
         f->eax = write(*(cpyEsp + 1), *(cpyEsp + 2), *(cpyEsp + 3));
         break;
      case SYS_SEEK:
         valid_ptr(cpyEsp + 1);
         valid_ptr(cpyEsp + 2);      
         seek(*(cpyEsp + 1), *(cpyEsp + 2));
         break;
      case SYS_TELL:
         valid_ptr(cpyEsp + 1);
         f->eax = tell(*(cpyEsp + 1));
         break;
      case SYS_CLOSE:
         valid_ptr(cpyEsp + 1);
         close(*(cpyEsp + 1));
         break;
      case SYS_CHDIR:
         valid_ptr(*(cpyEsp + 1));
         f->eax = chdir(*(cpyEsp + 1));
         break;
      case SYS_MKDIR:
         valid_ptr(*(cpyEsp + 1));
         f->eax = mkdir(*(cpyEsp + 1));
         break;
      case SYS_READDIR:
         valid_ptr((cpyEsp + 1));
         valid_ptr(*(cpyEsp + 2));  
         f->eax = readdir(*(cpyEsp + 1), *(cpyEsp + 2));
         break;
      case SYS_ISDIR:
         valid_ptr((cpyEsp + 1));
         f->eax = isdir(*(cpyEsp + 1));
         break;
      case SYS_INUMBER:
         valid_ptr((cpyEsp + 1));
         f->eax = inumber(*(cpyEsp + 1));
         break;
      default:
         thread_exit();
         break;
   }
   // End of Nathan driving
}


/* Terminates Pintos by calling shutdown_power_off() 
  (declared in devices/shutdown.h). */
void halt (void) {
   // Jeanie driving
   shutdown_power_off();
}


/* Terminates the current user program, returning status to the kernel. */
void exit (int status) {
   thread_current()->exit_status = status;
   sema_down(&file_access);

   // close current executing file
   if (thread_current()->save_file) {
      file_close(thread_current()->save_file);
   }

   // Jeanie and Nathan driving
   // close all open files
   for (int i = 2; i < 130; i++) {
      if (thread_current()->open_files[i]) {
         // Alan driving
         struct inode *inode = file_get_inode(thread_current()->open_files[i]);
         if (inode_dir(inode)) {
            dir_close(thread_current()->open_files[i]);
         } else {
            file_close (thread_current()->open_files[i]);
         }
         // End of Alan driving
         thread_current()->open_files[i] = NULL;       
      }
   }
   // End of Jeanie and Nathan

   // Cecilia driving
   // close current working dir
   if (thread_current()->dir) {
      dir_close(thread_current()->dir);
   }
   // End of Cecilia driving
   
   char* t;
   printf("%s: exit(%d)\n", strtok_r(&thread_current()->name, " ", &t), 
          status);
   sema_up(&file_access);
   thread_exit();
   // End of Jeanie driving
}


/* Runs the executable whose name is given in cmd_line, passing any given 
   arguments, and returns the new process's program id (pid). Must return 
   pid -1, which otherwise should not be a valid pid, if the program cannot
   load or run for any reason. */
pid_t exec (const char *cmd_line) {
   // Alan driving
   char *cmdCpy = cmd_line;
   
   for (int i = 0; !cmdCpy[i] && cmdCpy[i] != "\0"; i++) {
      valid_ptr(&cmdCpy[i]);
   }
   
   int tid = process_execute(cmd_line);
   return tid;
}


/* Waits for a child process pid and retrieves the child's exit status. */
int wait (pid_t pid) {
   return process_wait(pid);
}


/* Creates a new file called file initially initial_size bytes in size. 
   Returns true if successful, false otherwise. */
bool create (const char *file, unsigned initial_size) {
   sema_down(&file_access);
   bool result = filesys_create(file, (off_t) initial_size, false);
   sema_up(&file_access);
   return result;
}


/* Deletes the file called file. Returns true if successful, 
   false otherwise. */
bool remove (const char *file) {
   sema_down(&file_access);
   bool result = filesys_remove(file);
   sema_up(&file_access);
   return result;
   // End of Alan driving
}


/* Opens file called file. Returns a nonnegative integer handle called a
   "file descriptor" (fd) or -1 if the file could not be opened. */
int open (const char *file) { 
   // Cecilia driving
   struct file *openedFile = filesys_open(file);

   // file does not exist
   if (!openedFile) {
      return -1;
   }
   
   // store file in open_files array
   sema_down(&file_access);
   for (int i = 2; i < 130; i++) {
      if (!thread_current()->open_files[i]) {
         thread_current()->open_files[i] = openedFile;
         sema_up(&file_access);
         return i;
      }
   }   
   sema_up(&file_access);
   return -1;
}


/* Returns the size, in bytes, of the file open as fd. */
int filesize (int fd) {
   // checks for invalid fd: STDIN, STDOUT, or greater than open files index
   if (fd < 2 || fd > 129 || !thread_current()->open_files[fd]) {
      return 0;
   } else if (inode_dir(file_get_inode(thread_current()->open_files[fd])))
      return -1;
   else {
      sema_down(&file_access);
      int result = (int) file_length(thread_current()->open_files[fd]);
      sema_up(&file_access);
      return result;
   }
   // End of Cecilia driving
}


/* Reads size bytes from the file open as fd into buffer. Returns the number 
   of bytes actually read (0 at end of file), or -1 if the file could not be 
   read (due to a condition other than end of file). fd 0 reads from the 
   keyboard using input_getc(). */
int read (int fd, void* buffer, unsigned size) {  
   int bits = -1;
   if (fd < 0 || fd > 129 || !buffer || (!thread_current()->open_files[fd])) {
      return bits;
   }
   
   // validates buffer
   uint32_t buf_int = (uint32_t) buffer;
   while (buf_int < (uint32_t) buffer + size - 1) {
      valid_ptr((void*) buf_int);
      buf_int += PGSIZE;
   }

   // validates last byte address
   void* check_buf = (void*) ((uint32_t) buffer + size - 1);
   if (!check_buf || is_kernel_vaddr(check_buf) || 
       !pagedir_get_page(thread_current()->pagedir, check_buf)) {
      exit(-1);
   } 
   // End of Nathan driving
   
   // Jeanie driving
   sema_down(&file_access);

   if (fd == 1) {  // stdout
      sema_up(&file_access);
      exit(-1);
   } else if (fd == 0) {  // stdin
      for (int i = 0; i < size; i++) {
         char *buf = (char *) buffer;
         *(buf + i) = input_getc();
      }
      bits = size;
   } else {
      if (inode_dir(file_get_inode(thread_current()->open_files[fd]))){
         exit(-1);
      }
      bits = file_read(thread_current()->open_files[fd], buffer, size);
   } 
   sema_up(&file_access);
   return bits;
   // End of Jeanie driving
}


/* Writes size bytes from buffer to the open file fd. Returns the number of 
   bytes actually written, which may be less than size if some bytes could not
   be written. */
int write (int fd, const void *buffer, unsigned size) {
   // Alan driving
   if (fd < 1 || fd > 129 || !buffer || size == 0) {
      return 0;
   }

   if (!thread_current()->open_files[fd]) {
      exit(-1);
   }
   
   // Alan and Cecilia driving
   // validates buffer
   uint32_t buf_int = (uint32_t) buffer;
   while (buf_int < (uint32_t) buffer + size - 1) {
      valid_ptr((void*) buf_int);
      buf_int += PGSIZE;
   }
   valid_ptr((void*) ((uint32_t) buffer + size - 1));

   sema_down(&file_access);
   if (fd == 1) {  // stdin
      putbuf(buffer, size);
   } else {
      // can't write to directories
      if (inode_dir(file_get_inode(thread_current()->open_files[fd]))) {
         size = -1;
      } else {
         size = file_write (thread_current()->open_files[fd], buffer, size); 
      }
   }
   sema_up(&file_access);
   return size;
   // End of Alan and Cecilia driving
}


/* Changes the next byte to be read or written in open file fd to position, 
   expressed in bytes from the beginning of the file. (Thus, a position of 0
   is the file's start.) */
void seek (int fd, unsigned position) {
   // Cecilia driving
   if (fd < 1 || fd > 129 || !thread_current()->open_files[fd]) {
      exit(-1);
   }

   // seek only files that are not directories
   if (!inode_dir(file_get_inode(thread_current()->open_files[fd]))) {
      sema_down(&file_access);
      file_seek (thread_current()->open_files[fd], position);
      sema_up(&file_access);
   }
}


/* Returns the position of the next byte to be read or written in open file fd,
   expressed in bytes from the beginning of the file. */
unsigned tell (int fd) {
   if (fd < 2 || fd > 129 || !thread_current()->open_files[fd]) {
      return 0;
   }
   
   int result = -1;
   sema_down(&file_access);

   if (inode_dir(file_get_inode(thread_current()->open_files[fd]))) {
      sema_up(&file_access);
      return result;
   }

   result = (unsigned) file_tell(&thread_current()->open_files[fd]);
   sema_up(&file_access);
   return result;
}


/* Closes file descriptor fd. Exiting or terminating a process implicitly 
   closes all its open file descriptors, as if by calling this function for 
   each one. */
void close (int fd) {
   if (fd < 2 || fd > 129 || !thread_current()->open_files[fd]) {
      return;
   }

   sema_down(&file_access);
   struct inode* inode = file_get_inode(thread_current()->open_files[fd]);

   // close open file or directory
   if (inode_dir(inode)) {
      dir_close(thread_current()->open_files[fd]);
   } else {
      file_close (thread_current()->open_files[fd]);
   }
   thread_current()->open_files[fd] = NULL;
   sema_up(&file_access);
   // End of Cecilia driving
}

/* Changes the current working directory of the process to dir, which may
   be relative or absolute. Returns true if successful, false on failure. */
bool chdir (const char* path)
{
   // Jeanie and Nathan driving
   struct dir *dir = get_dir(path);
   char *name = get_file_name(path);
  
   if (!dir) {
      return false;
   }

   if (strcmp(name, "..") == 0) {  // parent directory
      dir = dir_open(dir_parent(dir));
   } else if (strcmp(name, ".") == 0 || 
              (is_root(dir) && strlen(name) == 0)) {
      // set current thread's directory to path
      thread_current()->dir = dir;
   } else {  
      struct inode *inode;
      dir_lookup(dir, name, &inode);
      if (inode) {
         dir = dir_open(inode);
      } else {
         // inode not found
         dir_close(dir);
         free(name);
         return false;
      }
   }

   dir_close(thread_current()->dir);
   thread_current()->dir = dir;
   free(name);

   return true;
   // End of Jeanie and Nathan driving
}

// Alan and Cecilia driving
/* Creates the directory named dir, which may be relative or absolute.
   Returns true if successful, false on failure. */
bool mkdir (const char* path)
{
   return filesys_create(path, 0, true);
}

/* reads directory into path */
bool readdir (int fd, char* path)
{
   struct file *file = thread_current()->open_files[fd];
   struct inode *inode = file_get_inode(file);

   if (!file || !inode || !inode_dir(inode)) {
      return false;
   }

   struct dir *dir = (struct dir*) file;
   return dir_readdir(dir, path);
}

/* Returns true if fd represents a directory, false if 
   it represents an ordinary file */
bool isdir (int fd)
{
   if (fd < 0 || fd > 129 || !thread_current()->open_files[fd]) {
      return -1;
   }

   struct file *file = thread_current()->open_files[fd];

   if (!file) {
      return false;
   }

   struct inode *inode = file_get_inode(file);
   return inode && inode_dir(inode);
}

/* Returns the inode number of the inode associated with fd, 
   which may represent an ordinary file or a directory. */
int inumber (int fd)
{
   struct file *file = thread_current()->open_files[fd];
   struct inode *inode = file_get_inode(file);

   if (fd < 0 || fd > 129 || !file || !inode) {
      return -1;
   }

   return inode_get_inumber(inode);
}
// End of Alan and Cecilia driving
