#include <proctable.h>
#include <synch.h>
#include <kern/errno.h>
#include <types.h>

struct proc *proctable[PID_MAX];

/*
 * Add the process to the proctable.
 */
int proctable_add(struct proc *process) {
    
    /* Find a free spot in the proctable. */
    pid_t i;
    pid_t next_pid = -1;
    for(i = 1; i < 256; i++) {
        if(proctable[i] == NULL) {
            next_pid = i;
            break;
        } 
    }
   
    if(next_pid == -1) {
        kprintf("next_pid is -1!\n");
    } 

    /* Assign the fields of the new proctable_entry. */
    process->p_pid = next_pid;
    process->p_exited = 0;
    process->p_exitcode = -1;
    process->p_sem  = sem_create("p_sem", 0);
    if(process->p_sem == NULL) {
        kprintf("pte_sem is NULL!\n");
        return 0;
    }
   
    /* Put the proctable_entry into the proctable. */
    proctable[next_pid] = process;
    return 1;
}

/*
 * Remove the process from the proctable.
 */
int proctable_remove(pid_t pid) {
    if(proctable[pid] == NULL) {
        kprintf("Proctable[pid] already NULL!\n");
        return 0;
    }
    
    /* Destroy the proctable_entry */
    proc_destroy(proctable[pid]);
    proctable[pid] = NULL;
    return 1;
}
