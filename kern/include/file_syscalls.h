#ifndef _FILE_SYSCALLS_H_
#define _FILE_SYSCALLS_H_

int sys_open(const char* fileName, int flags, int *retval);
int sys_close(int fd);
int sys_read(int fd, void *readbuff, size_t buflen, int *retval);
int sys_write(int fd, const void *writebuf, size_t nbytes, int *retval);
off_t sys_lseek(int fd, off_t pos, int whence);
int sys_dup2(int oldfd, int newfd);
int sys_chdir(const char *pathname);
int sys_getcwd(char *buf, size_t buflen);

#endif /* _FILE_SYSCALLS_H_ */
