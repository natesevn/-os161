#include <proc.h>
#include <synch.h>
#include <types.h>

/* Global process table */
extern struct proc *proctable[PID_MAX];

/* Functions for process table management. */
int proctable_add(struct proc *process);
int proctable_remove(pid_t pid);
