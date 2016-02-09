#include <types.h>
#include <copyinout.h>
#include <syscalls.h>

int
sys___open(const char* fileName, int flags)
{
	// check if filename is valid
	// check if flags are valid

	// scan fdtable and find the smallest index with a null field

	// copy filename into kernel using copyinstr, call vfs_open
	// vfs_open success, initialize struct for file
	// when setting offset, check the O_APPEND flag (should be filesize if set)
	// can use VOP_stat to get file size
}

int 
sys___close(int fd)
{
	// decrease file reference counter
	// close the file using vfs_close
	// free the struct
}

int
sys___read(int fd, static char readbuf[], int buflen)
{
	// use vop_read with struct iovec and struct uio
	// refer to loadelf.c
}

int 
sys___write(int fd, static char writebuf[], int buflen)
{
	// use vop_write with struct iovec and struct uio
	// refer to loadelf.c
}
