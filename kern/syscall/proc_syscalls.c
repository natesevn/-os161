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
    char **karg, **tempStrPtr;
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
    numArgs++;     
    
    /* Copy in user arguments to kernel buffer. */
    karg = (char **)kmalloc(numArgs*sizeof(char *));
    if(karg == NULL) {
        kfree(progname);
        return ENOMEM;
    }

    for(i=0; i<numArgs-1; i++) {
        tempStrPtr = (char **)kmalloc(sizeof(char *));
        if(tempStrPtr == NULL) {
            kfree(progname);
            kfree(karg);
            return ENOMEM;
        }

        tempArg = (char *)kmalloc(sizeof(char)*ARG_MAX);
        if(tempStrPtr == NULL) {
            kfree(progname);
            kfree(karg);
            kfree(tempStrPtr);
            return ENOMEM;
        }

        /* Copy in pointer pointing to a string pointer. */
        copysuccess = copyin((const_userptr_t)args, tempStrPtr, sizeof(char *));
        if(copysuccess) {
            kfree(progname);
            kfree(karg);
            kfree(tempStrPtr);
            kfree(tempArg);
            return copysuccess;
        }
        args += sizeof(char *);

        /* Copy in string pointed to by copied pointer. */
        copysuccess = copyinstr((const_userptr_t)*tempStrPtr, tempArg, PATH_MAX, &originalLen);
        if(copysuccess) {
            kfree(progname);
            kfree(karg);
            kfree(tempStrPtr);
            kfree(tempArg);
            return copysuccess;
        }

        /* Make provisions for aligning user arguments in kernel buffer. */
        len = originalLen;
        if(len%4 != 0) {
            len = len + (4 - len%4);
        }

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
        kfree(tempStrPtr); 
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
        len = strlen(karg[i]);

        stackptr = stackptr - len;
        copysuccess = copyoutstr(karg[i], (userptr_t)stackptr, len, NULL);
        if(copysuccess) {
            kfree(progname);
            kfree(karg);
            return copysuccess;
        }
        karg[i] = (char *)stackptr;
    }   

    /* Copy each string pointer address from kernel to user stack. */
    for(i=numArgs; i>=0; i--) {
        stackptr = stackptr - sizeof(char *);
        copysuccess = copyout(karg[i], (userptr_t)stackptr, sizeof(char *));
        if(copysuccess) {
            kfree(progname);
            kfree(karg);
            return copysuccess;
        }
    }

    kfree(progname);
    kfree(karg);
    
    /* Return to user mode. */
    enter_new_process(0, NULL, NULL, stackptr, entrypoint);
    
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

    if(status == NULL) {
        return EFAULT;
    }
    
    if(pid < PID_MIN || pid > PID_MAX) {
        return ESRCH;
    }
    
    if(proctable[pid]->pte_proc->p_ppid != curproc->p_pid) {
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
    proctable[curproc->p_pid]->pte_exited = 1; 
    proctable[curproc->p_pid]->pte_exitcode = exitcode;
    V(proctable[curproc->p_pid]->pte_sem);
    thread_exit();    
}
