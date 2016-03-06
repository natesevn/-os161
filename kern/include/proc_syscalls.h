#ifndef _PROC_SYSCALLS_H_
#define _PROC_SYSCALLS_H_

pid_t sys_fork(struct trapframe *tf, int *retval);
int sys_execv(const char *program, char **args);
pid_t sys_getpid(void);
pid_t sys_waitpid(pid_t pid, int *status, int options, int *retval);
void sys__exit(int exitcode);

#endif /* _PROC_SYSCALLS_H_ */
