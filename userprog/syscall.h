#include "threads/synch.h";
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
// Jeanie driving
extern struct semaphore file_access;
// End of Jeanie driving

#endif /* userprog/syscall.h */
