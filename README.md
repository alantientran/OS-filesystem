# CS 439 OS File System
This multi-part project consists of 4 different PintOS sections.

## Threads
* Create initial kernel thread and set thread states (i.e.- running, idle, blocked) inside semaphores to prevent race condiitons
* Scheduling, thread initiations, and tid allocations are implemented inside locks to ensure data consistency of shared resources
* Priority scheduling is implemented to consider priority inversions, donations, and nested donations
* Implement thread unblocking to transition a thread from a blocked to ready-to-run state. Does not pre-empt running threads in the case where a caller has disabled interupts
  
## User Programs
* Extend process_execute to handle argument passing
* Parse input for file (program) names and arguments
* Implement system calls like halt, exit, wait, open, read, pid_t exec, pid_t exec, etc.
* Deny writes to executables to prevent writes to an open file
  
## Virtual Memory
* Creating and modifying pages, frames, swap tables, and swap slots (disk memory)
* Demand paging for segments loaded from executables
* Handle page faults for reading non-resident pages and using pinning to prevent a frame's page from eviction
* Parallelism to prevent a page fault in the I/O from preventing other processes from executing
* Load segments into the stack after validating that the segment's virtual memory is within the user address space
* Stack growth to allow large programs their functions and store variables


## File System
* Multi-level indexed file system structure with 5 direct pointers, 8 indirect pointers, and 1 doubly indirect pointer
* Structured to allow quick access for small files through the direct pointers and accommodate larger files via file growth
* Max file size supported is 8.91 MB
* Implement hierarchical subdirectory system along with directory open and remove methods
* Locks are in place for any methods accessing the file system for consistency; however, multiple reads to a single file are allowed
* File extensions and writing data into a new section are atomic operations
