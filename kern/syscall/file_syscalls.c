#include <types.h>
#include <copyinout.h>
#include <syscall.h>
#include <limits.h>
#include <current.h>
#include <file_syscalls.h>
#include <uio.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <kern/seek.h>
#include <vfs.h>
#include <synch.h>
#include <filetable.h>
#include <proc.h>
#include <vnode.h>
#include <stat.h>
    
int
sys_open(const userptr_t filename, int flags, int *retval)
{
    // Copy in the string from user level to kernel level.
    // copyinstr handles EFAULT and ENAMETOOLONG errors 
    char filenamebuffer[NAME_MAX];
    int copysuccess = copyinstr(filename, filenamebuffer, NAME_MAX, NULL);

    // Check if filename is invalid
    if(copysuccess != 0) {
        return copysuccess;
    }

    // Check if flags are invalid
    if(flags < 0 || flags > 255) {
        return EINVAL;
    }

    // Create new filetable_entry
    struct filetable_entry *fte = (struct filetable_entry *) 
                                    kmalloc(sizeof(struct filetable_entry));
    struct vnode *newvnode = NULL; 
    fte->fte_vnode = newvnode;
    fte->fte_filename = filenamebuffer;
    fte->fte_refcount = 1;
    fte->fte_offset = 0;
    fte->fte_permissions = flags;
    fte->fte_lock = lock_create("fte_lock");

    // Acquire the filetable lock
    lock_acquire(curproc->filetable_lock);

    // Iterate through filetable to find a free slot
    int i;
    int fd; 
    for(i = 0; i < OPEN_MAX; i++) {
        if(curproc->filetable[i] == NULL) {
            fd = i;
            break;
        }
    }

    // Check if we actually found an empty slot
    if(i == OPEN_MAX) {
        return EMFILE;
    }
    
    // Call vfs_open after securing the lock for the filetable entry
    lock_acquire(fte->fte_lock);
    int opensuccess = vfs_open(filenamebuffer, flags, 0, &fte->fte_vnode);
    lock_release(fte->fte_lock);

    // Check if open was unsuccessful
    if(opensuccess != 0) {
        return opensuccess;
    }

    // Place the new entry in the filetable
    curproc->filetable[fd] = fte;

    // Release the filetable lock
    lock_release(curproc->filetable_lock);

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
    
    // Decrement our reference counter and call vfs_close if it is 0.
    lock_acquire(curproc->filetable[fd]->fte_lock);
    curproc->filetable[fd]->fte_refcount--;
    if(curproc->filetable[fd]->fte_refcount == 0) {
        vfs_close(curproc->filetable[fd]->fte_vnode);

        // We have to release the lock before freeing the entry
        lock_release(curproc->filetable[fd]->fte_lock);
        kfree(curproc->filetable[fd]);

        // Set the entry to NULL to be explicit
        lock_acquire(curproc->filetable_lock);
        curproc->filetable[fd] = NULL;
        lock_release(curproc->filetable_lock);
    }
    else {
        // If the refcount is still > 0, then we have not released the
        // filetable entry's lock yet, so do it here.
        lock_release(curproc->filetable[fd]->fte_lock);
    }

    return 0;
}

int
sys_read(int fd, userptr_t readbuf, size_t buflen, int *retval)
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
    newiov.iov_len = buflen;
    newiov.iov_ubase = readbuf;
    
    // Create a new uio struct
    struct uio newuio;
    newuio.uio_iov = &newiov;
    newuio.uio_iovcnt = 1;
    newuio.uio_offset = curproc->filetable[fd]->fte_offset;
    newuio.uio_resid = buflen;
    newuio.uio_segflg = UIO_USERSPACE;
    newuio.uio_rw = UIO_READ;
    newuio.uio_space = curproc->p_addrspace;
   
    // Call VOP_READ and update the filetable entry's offset
    lock_acquire(curproc->filetable[fd]->fte_lock);  
    int readsuccess = VOP_READ(curproc->filetable[fd]->fte_vnode, &newuio);
    
    // Checking for errors in the read
    if(readsuccess != 0) {
        return readsuccess;
    }

    int oldoffset = curproc->filetable[fd]->fte_offset;
    int newoffset = newuio.uio_offset;
    curproc->filetable[fd]->fte_offset = newoffset;
    lock_release(curproc->filetable[fd]->fte_lock);

    // Return the amount of bytes read in the retval variable by reference,
    // and return 0 in the function itself.
    *retval = newoffset - oldoffset;
    return 0;
}

int 
sys_write(int fd, const userptr_t writebuf, size_t nbytes, int *retval)
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
    newiov.iov_len = nbytes;
    newiov.iov_ubase = writebuf;
    
    // Create a new uio struct
    struct uio newuio;
    newuio.uio_iov = &newiov;
    newuio.uio_iovcnt = 1;
    newuio.uio_offset = curproc->filetable[fd]->fte_offset;
    newuio.uio_resid = nbytes;
    newuio.uio_segflg = UIO_USERSPACE;
    newuio.uio_rw = UIO_WRITE;
    newuio.uio_space = curproc->p_addrspace;
 
    // Call VOP_WRITE and update the filetable entry's offset
    lock_acquire(curproc->filetable[fd]->fte_lock);
    int writesuccess = VOP_WRITE(curproc->filetable[fd]->fte_vnode, &newuio);
    
    // Checking for errors in the write
    if(writesuccess != 0) {
        return writesuccess;
    }

    int oldoffset = curproc->filetable[fd]->fte_offset;
    int newoffset = newuio.uio_offset;

    off_t checkoffset = newuio.uio_offset;
    int32_t upperbits = (int32_t)((checkoffset >> 32) & 0x00000000FFFFFFFF);
    
    if(upperbits != 0x0) {
        lock_release(curproc->filetable[fd]->fte_lock);
        return EFBIG;
    }

    curproc->filetable[fd]->fte_offset = newoffset;
    lock_release(curproc->filetable[fd]->fte_lock); 

    // Return the amount of bytes written in the retval variable by reference,
    // and return 0 in the function itself.
    *retval = newoffset - oldoffset;
    return 0;
}

off_t
sys_lseek(int fd, off_t pos, int whence, off_t *retval64)
{
    // Check if fd and whence are invalid
    if(fd >= OPEN_MAX || fd < 0 || curproc->filetable[fd] == NULL) {
        return EBADF;
    }
    if(whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return EINVAL;
    }
    
    // Check if the file is seekable
    lock_acquire(curproc->filetable[fd]->fte_lock);
    bool seekable = VOP_ISSEEKABLE(curproc->filetable[fd]->fte_vnode);
    lock_release(curproc->filetable[fd]->fte_lock);

    if(!seekable) {
        return ESPIPE;
    }

    // Get the position of the end of file
    lock_acquire(curproc->filetable[fd]->fte_lock);
    struct stat newstat;
    int statsuccess = VOP_STAT(curproc->filetable[fd]->fte_vnode, &newstat);
    lock_release(curproc->filetable[fd]->fte_lock);

    // Check if the stat failed
    if(statsuccess != 0) {
        return statsuccess;
    }
    
    // Change the offset, depending on what the value of whence is
    lock_acquire(curproc->filetable[fd]->fte_lock);
    switch(whence) {
        case SEEK_SET:
            if(pos < 0) {
                return EINVAL;
            }
            curproc->filetable[fd]->fte_offset = pos;
            break;

        case SEEK_CUR:
            if(curproc->filetable[fd]->fte_offset + pos < 0) {
                return EINVAL;
            }
            curproc->filetable[fd]->fte_offset += pos;
            break;

        case SEEK_END:
            if(newstat.st_size + pos < 0) {
                return EINVAL;
            }
    
            kprintf("newstat.st_size: %llx\n", newstat.st_size);
            kprintf("pos: %llx\n", pos);
            curproc->filetable[fd]->fte_offset = newstat.st_size + pos;
            break;

        default:
            break;
    }
    lock_release(curproc->filetable[fd]->fte_lock);
   
    // Return the amount of bytes read in the retval variable by reference,
    // and return 0 in the function itself.
    off_t newoffset = curproc->filetable[fd]->fte_offset; 
    *retval64 = newoffset;

    return 0;
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
    if(curproc->filetable[newfd] != NULL) {
        sys_close(newfd);
    }

    // Increase the reference count and 
    // duplicate the file into filetable[newfd]
    lock_acquire(curproc->filetable[oldfd]->fte_lock);
    curproc->filetable[oldfd]->fte_refcount++;
    lock_acquire(curproc->filetable_lock);
    curproc->filetable[newfd] = curproc->filetable[oldfd];
    lock_release(curproc->filetable_lock);
    lock_release(curproc->filetable[oldfd]->fte_lock);

    // Return the newfd in the retval variable by reference,
    // and return 0 in the function itself.
    *retval = newfd;
    return 0;
}

int 
sys_chdir(const userptr_t pathname)
{
    // Copy in the string from user level to kernel level.
    // copyinstr handles EFAULT and ENAMETOOLONG errors 
    char pathnamebuffer[PATH_MAX];
    int copysuccess = copyinstr(pathname, pathnamebuffer, PATH_MAX, NULL);

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
sys_getcwd(userptr_t buf, size_t buflen)
{
    // Create a new iovec struct
    struct iovec newiov;
    newiov.iov_len = buflen;
    newiov.iov_ubase = buf;
    
    // Create a new uio struct
    struct uio newuio;
    newuio.uio_iov = &newiov;
    newuio.uio_iovcnt = 1;
    newuio.uio_offset = 0;
    newuio.uio_resid = buflen;
    newuio.uio_segflg = UIO_USERSPACE;
    newuio.uio_rw = UIO_READ;
    newuio.uio_space = curproc->p_addrspace;
    
    // Call vfs_getcwd and check for errors
    int getcwdsuccess = vfs_getcwd(&newuio);
    if(getcwdsuccess != 0) {
        return getcwdsuccess;
    }
    
    return 0;  
}


