#include <proc.h>
#include <syscall.h>
#include <limits.h>

/*
 * Fork syscall. 
 */
pid_t sys_fork(struct trapframe *tf, int *retval) {
    struct thread *child_thread;
    struct proc *child_proc;
    int index, success;
    char *proc_name, *thread_name;

    /*
     * Create child process.
     */
    proc_name = strcat(proc_name, curproc->p_name);
    proc_name = strcat(proc_name, "_child");
    child_proc = proc_create_runprogram(proc_name);
    if(child_proc = NULL) {
        return ENOMEM;
    }

    /*
     * Copy parent's trapframe.
     */
    struct trapframe *child_tf = kmalloc(sizeof(struct trapframe));
    if(tf = NULL) {
        return ENOMEM;
    }
    child_tf = tf;

    /*
     * Copy parent's address space.
     */
    struct addrspace *child_as = kmalloc(sizeof(struct addrspace));
    if(child_as = NULL) {
        kfree(child_tf);
        return ENOMEM;
    }
    
    success = as_copy(curproc->p_addrspace, &child_as);
    if(success != 0) {
        kfree(child_tf);
        kfree(child_as);
        return ENOMEM;
    }

    /*
     * Copy parent's filetable to child.
     */
    for(index=0; index<OPEN_MAX; i++) {
        child_proc->filetable[index] = curproc->filetable[index];
    }
    
    /*
     * Fork current thread and add it onto child process.
     */
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

    /*
     * Parent returns with child's pid.
     */
    child_proc->p_ppid = curproc->p_pid;
    *retval = child_proc->p_pid;

    return 0;
}

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
    child_tf->v0 = 0;
    /* Set register a3 to 0 to indicate success. */
    child_tf->a3 = 0;
    /* Advance program counter to prevent the recalling syscall. */
    child_tf->epc += 4;

    /* Load addrspace into child process */
    curproc->p_addrspace = child_as;
    as_activate(child_as);

    /* Copy trapframe onto current thread's stack, then go to usermode. */
    child_user_tf = &child_tf;
    mips_usermode(&child_user_tf);
    
}

int sys_execv(const char *program, char **args) {
    char **karg;
    char *progname; 
    int copysuccess, index;
    
    /*
     * Check for invalid pointer args.
     */
    if(program == NULL || args == NULL) {
        return EFAULT;
    }
    
    /*
     * Copy in program name from user mode to kernel.
     */
    copysuccess = copyinstr(program, progname, PATH_MAX, NULL);
    if(copysuccess != 0) {
        return copysuccess;
    }
    
    /*
     * Copy args from user space to kernel. 
     * Copy array in first, then copy each string in.
     */
    karg = (char **)kmalloc(sizeof(char **));
    copysuccess = copyin(args, karg, sizeof(char **));
    if(copysuccess != 0) {
        return copysuccess;
    }
    
    while(args[index] != NULL) {
        
    }
         

    /*
     * Open the exec, create new addrspace and load elf.
     */

    /*
     * Copy the args from kernel to user stack.
     */

    /*
     * Return to user mode.
     */
}
