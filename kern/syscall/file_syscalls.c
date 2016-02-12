#include <types.h>
#include <copyinout.h>
#include <syscalls.h>
#include <limits.h>
#include <current.h>

int
sys_open(const char* filename, int flags, int *retval)
{
    // Copy in the string from user level to kernel level.
    // copyinstr handles EFAULT and ENAMETOOLONG errors 
    char filenamebuffer[NAME_MAX];
    size_t *actual;
    int copysuccess = copyinstr(filename, filenamebuffer, NAME_MAX, actual);

    // Check if filename is invalid
    if(copysuccess != 0) {
        return copysuccess;
    }

    // Check if flags are invalid
    if(flags < 0 || flags > 255) {
        return EINVAL;
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
    fte->fte_lock = lock_create("fte_lock");

    // Acquire the filetable lock
    lock_acquire(&curproc->filetable_lock);

    // Iterate through filetable to find a free slot
    int i;
    int fd; 
    for(i = 0; i < OPEN_MAX; i++) {
        if(curproc->filetable[i] == NULL) {
            fd = i;
            break;
        }
    }
    
    // Call vfs_open after securing the lock for the filetable entry
    lock_acquire(&fte->fte_lock);
    int opensuccess = vfs_open(filenamebuffer, flags, 0, &fte->fte_vnode);
    lock_release(&fte->fte_lock);

    // Check if open was unsuccessful
    if(opensuccess != 0) {
        return opensuccess;
    }

    // Place the new entry in the filetable
    curproc->filetable[fd] = fte;

    // Release the filetable lock
    lock_release(&curproc->filetable_lock);

    // Pass the file descriptor in the return variable by reference, and 
    // return 0 in the function itself.
    *retval = fd;
    return 0;
}

int 
sys_close(int fd)
{
    // Check if fd is invalid
    if(fd >= OPEN_MAX || fd < 0 || curproc->filetable[fd] == NULL) {
        return EBADF;
    }
    
    // Call vfs_close and decrement our reference counter
    lock_acquire(&curproc->filetable[fd]->fte_lock);
    vfs_close(curproc->filetable[fd]->fte_vnode);
    curproc->filetable[fd]->fte_refcount--;
    lock_release(&curproc->filetable[fd]->fte_lock);

    // If reference counter is 0, we free up the slot in the filetable
    lock_acquire(&curproc->filetable_lock);
	if(curproc->filetable[fd]->fte_refcount == 0) {
        curproc->filtable[fd] = NULL;
    }
    lock_release(&curproc->filetable_lock);

    return 0;
}

int
sys_read(int fd, void *readbuf, size_t buflen, int *retval)
{
    // Check if fd is invalid
    if(fd >= OPEN_MAX || fd < 0 || curproc->filetable[fd] == NULL) {
        return EBADF;
    }
    
    // Check if the file is write only
    int permissions = curproc->filetable[fd]->fte_permissions & O_ACCMODE;
    if(permissions == O_WRONLY) { 
        return EBADF;
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
    lock_acquire(&curproc->filetable[fd]->fte_lock);
    VOP_READ(curproc->filetable[fd]->fte_vnode, newuio);
    int oldoffset = filetable[fd]->fte_offset;
    int newoffset = newuio->uio_offset;
    curproc->filetable[fd]->fte_offset = newoffset;
    lock_release(&curproc->filetable[fd]->fte_lock);

    // Return the amount of bytes read in the retval variable by reference,
    // and return 0 in the function itself.
    *retval = newoffset - oldoffset;
    return 0;
}

int 
sys_write(int fd, const void *writebuf, size_t nbytes, int *retval)
{
	// Check if fd is invalid
    if(fd >= OPEN_MAX || fd < 0 || curproc->filetable[fd] == NULL) {
        return EBADF;
    }
    
    // Check if the file is write only
    int permissions = curproc->filetable[fd]->fte_permissions & O_ACCMODE;
    if(permissions == O_RDONLY) { 
        return EBADF;
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
    lock_acquire(&curproc->filetable[fd]->fte_lock);
    VOP_WRITE(curproc->filetable[fd]->fte_vnode, newuio);
    int oldoffset = filetable[fd]->fte_offset;
    int newoffset = newuio->uio_offset;
    filetable[fd]->fte_offset = newoffset;
    lock_releasee(&curproc->filetable[fd]->fte_lock);

    // Return the amount of bytes written in the retval variable by reference,
    // and return 0 in the function itself.
    *retval = newoffset - oldoffset;
    return 0;
}

off_t
sys_lseek(int fd, off_t pos, int whence)
{
    // Check if fd and whence are invalid
    if(fd >= OPEN_MAX || fd < 0 || curproc->filetable[fd] == NULL) {
        return EBADF;
    }
    if(whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return EINVAL;
    }
    
    // Check if the file is seekable
    lock_acquire(&curproc->filetable[fd]->fte_lock);
    bool seekable = vop_isseekable(curproc->filetable[fd]->fte_vnode);
    lock_release(&curproc->filetable[fd]->fte_lock);

    if(!seekable) {
        return ESPIPE;
    }

    // TODO: CHANGE THE OFFSET AND UPDATE FTE_OFFSET

}

int 
sys_dup2(int oldfd, int newfd, int *retval)
{
    // Check if oldfd and newfd are invalid
    if(oldfd >= OPEN_MAX || oldfd < 0 || curproc->filetable[oldfd] == NULL) {
        return EBADF;
    }
    if(newfd >= OPEN_MAX || newfd < 0) {
        return EBADF;
    }

    // Close the file to free up the slot in newfd
    while(curproc->filetable[newfd] != NULL) {
        sys___close(newfd);
    }

    // Increasing the reference count and 
    // duplicate the file into filetable[newfd]
    lock_acquire(&curproc->filetable[oldfd]->fte_lock);
    curproc->filetable[oldfd]->fte_refcount++;
    lock_release(&curproc->filetable[oldfd]->fte_lock);

    lock_acquire(&curproc->filetable_lock);
    curproc->filetable[newfd] = curproc->filetable[oldfd];
    lock_release(&curproc->filetable_lock);

    // Return the newfd in the retval variable by reference,
    // and return 0 in the function itself.
    *retval = newfd;
    return 0;
}

int 
sys_chdir(const char *pathname)
{
    // Copy in the string from user level to kernel level.
    // copyinstr handles EFAULT and ENAMETOOLONG errors 
    char pathnamebuffer[PATH_MAX];
    size_t *actual;
    int copysuccess = copyinstr(pathname, pathnamebuffer, PATH_MAX, actual);

    // Check if filename is invalid
    if(copysuccess != 0) {
        return copysuccess;
    }

    // Call vfs_chdir
    int chdirsuccess = vfs_chdir(pathnamebuffer);
    
    // Check if vfs_chdir was unsuccessful    
    if(chdirsuccess != 0) {
        return chdirsuccess;
    }
    
    return 0; 
}

int 
sys_getcwd(char *buf, size_t buflen)
{
    // Create a new iovec struct
    struct iovec newiov;
    newiov->iov_len = buflen;
    newiov->iov_kbase = buf;
    
    // Create a new uio struct
    struct uio *newuio;
    newuio->uio_iov = newiov;
    newuio->uio_iovcnt = 1;
    newuio->uio_offset = 0;
    newuio->uio_resid = buflen;
    newuio->uio_segflg = UIO_USERSPACE;
    newuio->uio_rw = UIO_READ;
    newuio->uio_space = curproc;
    
    // Call vfs_getcwd and check for errors
    int getcwdsuccess = vfs_getcwd(newuio);
    if(getcwdsuccess != 0) {
        return getcwdsuccess;
    }
    
    return 0;  
}


