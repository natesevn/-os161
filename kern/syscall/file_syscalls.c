#include <types.h>
#include <copyinout.h>
#include <syscalls.h>
#include <limits.h>
#include <current.h>
#include <errno.h>

int
sys___open(const char* fileName, int flags, int mode)
{
    // Copy in the string from user level to kernel level.
    // copyinstr handles EFAULT and ENAMETOOLONG errors 
    char filenamebuffer[NAME_MAX];
    size_t *actual;
    int copysuccess = copyinstr(fileName, fileNameBuffer, NAME_MAX, actual);

    // Check if filename is invalid
    if(copysuccess != 0) {
        errno = copysuccess;
        return -1;
    }

    // Check if flags are invalid
    if(flags < 0 || flags > 255) {
        errno = EINVAL;
        return -1
    }

    // Iterate through filetable to find a free slot
    int i;
    int fd; 
    for(i = 0; i < OPEN_MAX; i++) {
        if(curproc->filetable[i] == NULL) {
            fd = i;
            break;
        }
    }
    
    // Create new filetable_entry
    struct filetable_entry fte;
    struct vnode *newvnode;
    struct lock *newlock;
    fte->fte_vnode = newvnode;
    fte->fte_filename = filenamebuffer;
    fte->fte_refcount = 1;
    fte->fte_offset = 0;
    fte->fte_permissions = flags;
    fte->fte_lock = newlock;

    // Check if existing file is already open. 
    // If it is already open, increase its refcount.
    for(i = 0; i < OPEN_MAX; i++) {
        if(!strcmp(curproc->filetable[i]->fte_filename, fte->fte_filename)) {
            curproc->filetable[i]->fte_refcount++;
            fte->fte_refcount = curproc->filetable[i]->fte_refcount;
        }
    }

    // Call vfs_open
    int opensuccess = vfs_open(filenamebuffer, flags, mode, &fte->fte_vnode);
    
    // Check if open was unsuccessful
    if(opensuccess != 0) {
        errno = opensuccess;
        return -1;
    }
    
    // Otherwise, we place the new entry in the filetable
    curproc->filetable[fd] = fte;
    return 0;
}

int 
sys___close(int fd)
{
    // Check if fd is invalid
    if(fd >= OPEN_MAX || fd < 0 || curproc->filetable[fd] == NULL) {
        errno = EBADF;
        return -1;
    }
    
    // Call vfs_close and decrement our reference counter
    vfs_close(curproc->filetable[fd]->fte_vnode);
    curproc->filetable[fd]->fte_refcount--;

    // If reference counter is 0, we free up the slot in the filetable
	if(curproc->filetable[fd]->fte_refcount == 0) {
        curproc->filtable[fd] = NULL;
    }

    return 0;
}

int
sys___read(int fd, void *readbuf, size_t buflen)
{
    // Check if fd is invalid
    if(fd >= OPEN_MAX || fd < 0 || curproc->filetable[fd] == NULL) {
        errno = EBADF;
        return -1;
    }
    
    // Check if the file is write only
    int permissions = curproc->filetable[fd]->fte_permissions & O_ACCMODE;
    if(permissions == O_WRONLY) { 
        errno = EBADF;
        return -1;
    }
    
    // Create a new iovec struct
    struct iovec newiov;
    newiov->iov_len = buflen;
    newiov->iov_kbase = readbuf;
    
    // Create a new uio struct
    struct uio newuio;
    newuio->uio_iov = newiov;
    newuio->uio_iovcnt = 1;
    newuio->uio_offset = filetable[fd]->fte_offset;
    newuio->uio_resid = buflen;
    newuio->uio_segflg = UIO_USERSPACE;
    newuio->uio_rw = UIO_READ;
    newuio->uio_space = curproc;
   
    // Call VOP_READ and update the filetable entry's offset
    VOP_READ(curproc->filetable[fd]->fte_vnode, newuio);
    int oldoffset = filetable[fd]->fte_offset;
    int newoffset = newuio->uio_offset;
    filetable[fd]->fte_offset = newoffset;

    // Return the amount of bytes read 
    return newoffset - oldoffset;
}

int 
sys___write(int fd, const void *writebuf, size_t nbytes)
{
	// Check if fd is invalid
    if(fd >= OPEN_MAX || fd < 0 || curproc->filetable[fd] == NULL) {
        errno = EBADF;
        return -1;
    }
    
    // Check if the file is write only
    int permissions = curproc->filetable[fd]->fte_permissions & O_ACCMODE;
    if(permissions == O_RDONLY) { 
        errno = EBADF;
        return -1;
    }

    // Create a new iovec struct
    struct iovec newiov;
    newiov->iov_len = buflen;
    newiov->iov_kbase = writebuf;
    
    // Create a new uio struct
    struct uio newuio;
    newuio->uio_iov = newiov;
    newuio->uio_iovcnt = 1;
    newuio->uio_offset = filetable[fd]->fte_offset;
    newuio->uio_resid = nbytes;
    newuio->uio_segflg = UIO_USERSPACE;
    newuio->uio_rw = UIO_WRITE;
    newuio->uio_space = curproc;
    
     // Call VOP_WRITE and update the filetable entry's offset
    VOP_WRITE(curproc->filetable[fd]->fte_vnode, newuio);
    int oldoffset = filetable[fd]->fte_offset;
    int newoffset = newuio->uio_offset;
    filetable[fd]->fte_offset = newoffset;

    // Return the amount of bytes written
    return newoffset - oldoffset;
}
