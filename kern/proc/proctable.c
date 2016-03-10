#include <proctable.h>
#include <synch.h>
#include <kern/errno.h>
#include <types.h>

struct proctable_entry *proctable[PID_MAX];
pid_t next_pid = 2;

/*
 * Add the process to the proctable.
 */
int proctable_add(struct proc *process) {
    /* Do some error checking. */
    if(next_pid == PID_MAX) {
        kprintf("next_pid is too big!\n");
        return 0;
    }

    if(proctable[next_pid] != NULL) {
        kprintf("proctable[next_pid] is not null!\n");
        return 0;
    }
    
    /* Create a new proctable_entry for the proctable. */
    struct proctable_entry *new_pte = kmalloc(sizeof(struct proctable_entry));
    
    if(new_pte == NULL) {
        kprintf("new_pte is NULL\n");
        return 0;
    }

    /* Assign the fields of the new proctable_entry. */
    process->p_pid = next_pid;
    new_pte->pte_proc = process;
    new_pte->pte_exited = 0;
    new_pte->pte_exitcode = -1;
    new_pte->pte_sem = sem_create("pte_sem", 0);
    if(new_pte->pte_sem == NULL) {
        kprintf("pte_sem is NULL!\n");
        return 0;
    }
   
    /* Put the proctable_entry into the proctable. */
    proctable[next_pid] = new_pte;
    next_pid++;
    return 1;
}

/*
 * Remove the process from the proctable.
 */
int proctable_remove(pid_t pid) {
    if(proctable[pid] == NULL) {
        kprintf("Proctable[pid] already NULL!\n");
        return 1;
    }
    
    /* Destroy the proctable_entry */
    if(proctable[pid]->pte_proc != NULL) {
        proc_destroy(proctable[pid]->pte_proc);
    }
    sem_destroy(proctable[pid]->pte_sem);
    kfree(proctable[pid]);
    return 1;
}
