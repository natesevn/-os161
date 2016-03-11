#include <proc.h>
#include <synch.h>
#include <types.h>

struct proctable_entry {
    struct proc *pte_proc;
    int pte_exited;
    int pte_exitcode;
    struct semaphore *pte_sem;
};

/* Global process table */
extern struct proctable_entry *proctable[32];

/* Global pid to assign new processes */
extern pid_t next_pid;

int proctable_add(struct proc *process);
int proctable_remove(pid_t pid);
