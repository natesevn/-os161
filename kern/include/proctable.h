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
extern struct proctable_entry *proctable[256];

/* Global pid to assign new processes */
//pid_t PTE_PID_LIMIT = 256;

int proctable_add(struct proc *process);
int proctable_remove(pid_t pid);
