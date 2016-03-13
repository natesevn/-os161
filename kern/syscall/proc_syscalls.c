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
    const char *proc_name = "JesseP";
    const char  *thread_name = "JesseT";
    
    /* Create child process. */
    child_proc = proc_create_runprogram(proc_name);
    if(child_proc == NULL) {
        return ENOMEM;
    }
    
    /* Copy parent's trapframe. */
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if(tf == NULL) {
        return ENOMEM;
    }
    memcpy(child_tf, tf, sizeof(struct trapframe));
    
    /* Copy parent's address space. */
    struct addrspace *child_as;
    success = as_copy(curproc->p_addrspace, &child_as);
    if(success != 0) {
        //kfree(child_tf);
        return ENOMEM;
    }
    
    /* Copy parent's filetable to child */
    for(index=0; index<OPEN_MAX; index++) {
        child_proc->filetable[index] = curproc->filetable[index];
    }
    
    /* Fork current thread and add it onto child process. */
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
 * SYSTEM CALL: EXECV
 *
 * Replaces the currently executing program with a newly loaded program image.
 * Does not return upon success. Instead,  the new program begins executing.
 * Returns an error upon failure.
 */
int sys_execv(const char *program, char **args) {
    struct vnode *v;
    struct addrspace *new_as, *old_as;
    vaddr_t entrypoint, stackptr;
    char **karg;
    char *progname, *tempArg;
    int copysuccess = 0;
    int i, j, numArgs, result; 
    size_t originalLen, len;
 
    /* Check for invalid pointer args. */
    if(program == NULL || args == NULL) {
        return EFAULT;
    }
    
    /* Copy in program name from user mode to kernel. */
    progname = (char *)kmalloc(sizeof(char) * PATH_MAX);
    if(progname == NULL) {
        kfree(progname);
        return ENOMEM;
    }
    copysuccess = copyinstr((const_userptr_t)program, progname, PATH_MAX, NULL);
    if(copysuccess) {
        kfree(progname);
        return copysuccess;
    }
    
    
    /* Loop to find number of arguments. */
    numArgs = 0; 
    while(args[numArgs] != NULL) { 
        numArgs++;
    }
    int argSizes[numArgs];  
    
    /* Copy in user arguments to kernel buffer. */
    karg = (char **)kmalloc(sizeof(char *));
    if(karg == NULL) {
        kfree(progname);
        return ENOMEM;
    }
    
    copysuccess = copyin((const_userptr_t)args, karg, sizeof(char *));
    if(copysuccess) {
        kfree(progname);
        kfree(karg);
        return copysuccess;
    }

    for(i=0; i<numArgs; i++) {

        tempArg = (char *)kmalloc(sizeof(char)*ARG_MAX);
        if(tempArg == NULL) {
            kfree(progname);
            kfree(karg);
            return ENOMEM;
        }

        /* Copy in string pointed to by copied pointer. */
        copysuccess = copyinstr((const_userptr_t)args[i], tempArg, ARG_MAX, &originalLen);
        if(copysuccess) {
            kfree(progname);
            kfree(karg);
            //kfree(tempStrPtr);
            kfree(tempArg);
            return copysuccess;
        }

        /* Make provisions for aligning user arguments in kernel buffer. */
        len = originalLen;
        if(len%4 != 0) {
            len = len + (4 - len%4);
        }
        argSizes[i] = len;

        /* Pad user argument with '\0' to align them. */
        karg[i] = (char *)kmalloc(len*sizeof(char));
        for(j=0; (size_t)j<len; j++) {
             if((size_t)j >= originalLen) {
                karg[i][j] = '\0';
            }
            else {
                karg[i][j] = tempArg[j];
            }
        }
        kfree(tempArg);
    }
    karg[i] = NULL;

    /* Open the exec, create new addrspace and load elf. */
    result = vfs_open(progname, O_RDONLY, 0, &v);
    if(result) {
        kfree(progname);
        kfree(karg);
        return result;
    }

    new_as = as_create();
    if(new_as == NULL) {
        kfree(progname);
        kfree(karg);
        vfs_close(v);
        return ENOMEM;
    }

    old_as = proc_setas(new_as);
    as_activate();
    as_destroy(old_as);

    result = load_elf(v, &entrypoint);
    if(result) {
        kfree(progname);
        kfree(karg);
        vfs_close(v);
        return result; 
    }

    vfs_close(v);

    /* Define user stack. */
    result = as_define_stack(new_as, &stackptr);
    if(result) {
        kfree(progname);
        kfree(karg);
        return result;
    }

    /* Copy each string from kernel to user stack. */
    for(i=numArgs-1; i>=0; i--) {
        stackptr = stackptr - argSizes[i];
        copysuccess = copyoutstr(karg[i], (userptr_t)stackptr, argSizes[i], NULL);
        if(copysuccess) {
            kfree(progname);
            kfree(karg);
            return copysuccess;
        }
        karg[i] = (char *)stackptr;
    }   

    /* Manually "copy" the NULL pointer in. */
    stackptr -= 4*sizeof(char);

    /* 
     * Copy each string pointer address from kernel to user stack. 
     * Ignore the NULL pointer because that causes problems.
     */
    for(i=numArgs-1; i>=0; i--) {
        stackptr = stackptr - sizeof(char *);
        copysuccess = copyout((karg + i), (userptr_t)stackptr, sizeof(char *));      
        if(copysuccess) {
            kfree(progname);
            kfree(karg);
            return copysuccess;
        }
    }

    kfree(progname);
    kfree(karg);

    /* Return to user mode. */
    enter_new_process(numArgs, (userptr_t) stackptr, NULL, stackptr, entrypoint);

    panic("enter_new_process returned\n");
    return EINVAL;
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

    if(pid < PID_MIN || pid > PID_MAX) {
        return ESRCH;
    }
    
    if(proctable[pid]->p_ppid != curproc->p_pid) {
        return ECHILD;
    }
    
    /* Wait for the child to exit. */
    if(proctable[pid]->p_exited == 0) {
        P(proctable[pid]->p_sem);
    }

    /* Copy exit status to status pointer and check for errors */
    int copy_result = copyout((const void *)&proctable[pid]->p_exitcode, 
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
    
    /* Change the proctable entry's fields to indicate an exit. */
    proctable[curproc->p_pid]->p_exited = 1; 
    proctable[curproc->p_pid]->p_exitcode = _MKWAIT_EXIT(exitcode);
    V(proctable[curproc->p_pid]->p_sem);
    
    /* Check if the parent has already exited. */   
    if(proctable[curproc->p_ppid]->p_exited == 1) { 
        struct proc *destroyproc = curproc;
        struct thread *movethread = curthread;

        /* Clean up any children that stuck around for its parent. */
        int i;
        for(i = 0; i < PID_MAX; i++) {
            if(proctable[i] != NULL) {
                if(proctable[i]->p_ppid == destroyproc->p_ppid
                    && proctable[i]->p_exited == 1
                    && proctable[i] != destroyproc) {
                    proctable_remove(proctable[i]->p_pid);
                }
            }
        }

        /* Detach the current thread and destroy the process. */
        proc_remthread(curthread);
        proc_addthread(kproc, movethread);
        proctable_remove(destroyproc->p_pid);
    }
    
    thread_exit(); 
}
