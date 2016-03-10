#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <limits.h>
#include <uio.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <kern/seek.h>
#include <vfs.h>
#include <synch.h>
#include <vnode.h>
#include <stat.h>
#include <current.h>
#include <proc.h>
#include <proctable.h>
#include <proc_syscalls.h>
#include <file_syscalls.h>
#include <filetable.h>
#include <thread.h>
#include <kern/wait.h>
#include <addrspace.h>
#include <mips/trapframe.h>

/*
 * Entry point for child process.
 * Modifies child's trapframe to make it return 0, and to indicate success.
 * Then loads addrspace before going into usermode.
 */
void child_entry(void *data1, unsigned long data2) {
    struct trapframe *child_tf, child_user_tf;
    struct addrspace *child_as;

    child_tf = (struct trapframe *)data1;
    child_as = (struct addrspace *)data2;

    /* Store child's return value in v0. */
    child_tf->tf_v0 = 0;
    /* Set register a3 to 0 to indicate success. */
    child_tf->tf_a3 = 0;
    /* Advance program counter to prevent the recalling syscall. */
    child_tf->tf_epc += 4;

    /* Load addrspace into child process */
    curproc->p_addrspace = child_as;
    as_activate();

    /* Copy trapframe onto current thread's stack, then go to usermode. */
    child_user_tf = *child_tf;
    mips_usermode(&child_user_tf);
    
}

/*
 * SYSTEM CALL: FORK
 *
 * Duplicates the currently running process.
 * Returns twice: once for the parent, once for the child. 
 */
pid_t sys_fork(struct trapframe *tf, int *retval) {
    struct proc *child_proc;
    int index, success;
    char *proc_name = NULL;
    char  *thread_name = NULL;

    /* Create child process. */
    proc_name = strcat(proc_name, curproc->p_name);
    proc_name = strcat(proc_name, "_child");
    child_proc = proc_create_runprogram(proc_name);
    if(child_proc == NULL) {
        return ENOMEM;
    }

    /* Copy parent's trapframe. */
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if(tf == NULL) {
        return ENOMEM;
    }
    child_tf = tf;

    /* Copy parent's address space. */
    struct addrspace *child_as = kmalloc(sizeof(struct addrspace));
    if(child_as == NULL) {
        kfree(child_tf);
        return ENOMEM;
    }
    
    success = as_copy(curproc->p_addrspace, &child_as);
    if(success != 0) {
        kfree(child_tf);
        kfree(child_as);
        return ENOMEM;
    }

    /* Copy parent's filetable to child */
    for(index=0; index<OPEN_MAX; index++) {
        child_proc->filetable[index] = curproc->filetable[index];
    }
    
    /* Fork current thread and add it onto child process. */
    thread_name = strcat(thread_name, curthread->t_name);
    thread_name = strcat(thread_name, "_child");
    success = thread_fork(thread_name, child_proc, child_entry, 
                         (struct trapframe *)child_tf, 
                         (unsigned long)child_as);    
    if(success != 0) {
        kfree(child_tf);
        kfree(child_as);
        proc_destroy(child_proc);
        return ENOMEM;
    }

    /* Parent returns with child's pid. */
    child_proc->p_ppid = curproc->p_pid;
    *retval = child_proc->p_pid;

    return 0;
}

/*
 * SYSTEM CALL: GETPID
 *
 * Returns the current process' id
 */
pid_t sys_getpid(void) {
    return curthread->t_proc->p_pid;
}

/*
 * SYSTEM CALL: WAITPID
 *
 * Waits for the process specified by pid to exit, then returns an encoded
 * exit status in the status pointer. 
 */
pid_t sys_waitpid(pid_t pid, int *status, int options, int *retval) {
    
    /* Do error checking on the arguments. */
    if(options != 0) {
        return EINVAL;
    }

    if(status == NULL) {
        return EFAULT;
    }
    
    if(pid < PID_MIN || pid > PID_MAX) {
        return ESRCH;
    }
    
    if(proctable[pid]->pte_proc.p_ppid != curproc->p_pid) {
        return ECHILD;
    }

    /* Wait for the child to exit. */
    if(proctable[pid]->pte_exited == 0) {
        P(proctable[pid]->pte_sem);
    }

    /* Copy exit status to status pointer and check for errors */
    int copy_result = copyout((const void *)&proctable[pid]->pte_exitcode, 
                        (userptr_t)status, sizeof(int));
    if(copy_result) {
        return copy_result;
    }

    /* Return the pid inside retval, and return 0 in the function. */
    *retval = pid;
    return 0;
}

/*
 * SYSTEM CALL: EXIT
 * 
 * Causes the current process to exit.
 * Does not return.
 */
void sys__exit(int exitcode) {
    
    /* Find the current process' parent. */
    int parent_index;
    int i;
    for(i = 0; i < PID_MAX; i++) {
        if(proctable[i]->pte_proc.p_pid == curproc->p_ppid) {
            parent_index = i;
            break;
        }
    }

    /* Only bother to fill the exitcode if the parent has
     * not exited yet. If the parent has already exited, destroy
     * the process, as we can be sure waitpid won't be called 
     * on it anymore.
     */
    if(proctable[parent_index]->pte_exited == 0) {
        proctable[curproc->p_pid]->pte_exitcode  = _MKWAIT_EXIT(exitcode);
        proctable[curproc->p_pid]->pte_exited = 1;
        V(proctable[curproc->p_pid]->pte_sem);
    } else {
        proc_destroy(curproc);
    }

    thread_exit();
}
