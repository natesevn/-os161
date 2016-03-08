#include <proc.h>
#include <synch.h>

struct proctable_entry {
    struct proc pte_proc;
    int pte_exited;
    int pte_exitcode;
    struct semaphore *pte_sem;
}

/* Global process table */
struct proctable_entry *proctable[PID_MAX];

