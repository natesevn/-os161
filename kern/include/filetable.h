#include <array.h>

struct filetable_entry {
    struct vnode *fte_vnode;
    char *fte_filename;
    int fte_refcount;
    int fte_offset;
    int fte_permissions;
    struct lock *fte_lock;
};

void filetable_create();
void filetable_expand();
void filetable_destroy();
