#ifndef _FILE_SYSCALLS_H_
#define _FILE_SYSCALLS_H_

int sys_open(const userptr_t filename, int flags, int *retval);
int sys_close(int fd);
int sys_read(int fd, userptr_t readbuf, size_t buflen, int *retval);
int sys_write(int fd, const userptr_t writebuf, size_t nbytes, int *retval);
off_t sys_lseek(int fd, off_t pos, int whence, off_t *retval64);
int sys_dup2(int oldfd, int newfd, int *retval);
int sys_chdir(const userptr_t pathname);
int sys_getcwd(userptr_t buf, size_t buflen);

#endif /* _FILE_SYSCALLS_H_ */
