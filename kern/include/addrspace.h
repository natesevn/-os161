/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */


#include <vm.h>
#include "opt-dumbvm.h"

struct vnode;

struct pagetable_entry {
    int pte_permissions;    /* valid(1), dirty(1), ref(1), protection(3) */
    vaddr_t pte_vaddr;      /* virtual address */
    paddr_t pte_paddr;      /* physical address */
};

struct region {
    vaddr_t as_vbase;
    paddr_t as_pbase;
    size_t as_npages;
};

/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */
struct addrspace {
#if OPT_DUMBVM
    vaddr_t as_vbase1;
    paddr_t as_pbase1;
    size_t as_npages1;
    vaddr_t as_vbase2;
    paddr_t as_pbase2;
    size_t as_npages2;
    paddr_t as_stackpbase;
#else
    /* Put stuff here for your VM system */
//    pagetable_entry *as_stack;      /* stack pagetable binary tree's root */
//    vaddr_t as_stack_start;         /* starting address of stack */
//    vaddr_t as_stack_end;           /* ending address of stack */

//    pagetable_entry *as_heap;       /* heap pagetable binary tree's root */
//    vaddr_t as_heap_start;          /* starting address of heap */
//    vaddr_t as_heap_end;            /* ending address of heap */

//    pagetable_entry *as_segments;   /* segment pagetable binary tree's root */ 
//    vaddr_t as_segments_start;      /* starting address of other segments */
//    vaddr_t as_segments_end;        /* ending address of other segments */

    /* DUMBVM assumes user will only ever use 2 regions. We fix that: */
    struct region* regionlist;

    /* DUMBVM assumes that there is no such thing as a heap. We fix that: */
    vaddr_t as_heap_start, as_heap_end;

    /* DUMBVM assumes that the stack will never grow. We fix that: */
    vaddr_t as_stack_start, as_stack_end;

    pagetable_entry *as_pages;

#endif
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 *
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor. This is used to
 *                avoid potentially "seeing" it while it's being
 *                destroyed.
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 *
 * Note that when using dumbvm, addrspace.c is not used and these
 * functions are found in dumbvm.c.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, size_t sz,
                                   int readable,
                                   int writeable,
                                   int executable);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);


/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);

/*
 * Helper functions
 *    pagetable_create - initialize page table; called by as_create
 *    pagetable_destroy - free pagetable; called by as_destroy
 *    get_page_table_entry - get the pagetable entry pointer (?)
 */

 void pagetable_create(struct pagetable_entry *pt);
 void pagetable_destroy(struct pagetable_entry *pt);
 void get_pagetable_entry(struct addrspace *as, vaddr_t vaddr);


#endif /* _ADDRSPACE_H_ */
