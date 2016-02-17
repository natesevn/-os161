#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <array.h>

/* The struct representing a file in userspace. Each entry in our filetable 
 * will be represented by one of these structs. 
 * fte_vnode: representation of a file in kernelspace
 * fte_filename: path/name of the file 
 * fte_refcount: number of threads working on the file
 * fte_offset: where in the file to start reading/writing from
 * fte_permissions: what can the thread do to this file
 * fte_lock: synchronization primitive for the file
 */
struct filetable_entry {
    struct vnode *fte_vnode;
    char *fte_filename;
    int fte_refcount;
    off_t fte_offset;
    int fte_permissions;
    struct lock *fte_lock;
};

#endif /* _FILETABLE_H_ */
