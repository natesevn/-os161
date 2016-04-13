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

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

#define DUMBVM_STACKPAGES    18

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

/*
 * Allocates space for the structure that holds information about the address
 * space.
 * Returns: allocated addrspace struct if successful
 			else returns NULL
 */
struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_heap_start = (vaddr_t)0;
	as->as_heap_end = (vaddr_t)0;
	as->as_stack_start = (vaddr_t)0;
	as->as_stack_end = (vaddr_t)0;
	as->regionlist = NULL;

	pagetable_create(as->as_pages);

	return as;
}

/*
 * Copy old addrspace into a new addrspace.
 * Returns: 0 upon success
 *          errno otherwise
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;
    int region_size, pagetable_size, i;

	/* Create new addrspace. */
    new = as_create();
	if (new == NULL) {
		return ENOMEM;
	}

    /* Copy region list into new addrspace. */ 
    region_size = sizeof(old->regionlist)/sizeof(old->regionlist[0]);
    new->regionlist = (struct region*)
                      kmalloc(region_size*sizeof(struct region));
    
    for(i = 0; i < region_size; i++) {
        new->regionlist[i].as_vbase = old->regionlist[i].as_vbase;
        new->regionlist[i].as_pbase = old->regionlist[i].as_pbase;
        new->regionlist[i].as_npages = old->regionlist[i].as_npages;
    }
    
	/* Allocate physical pages for the new addrspace. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

    /* Copy old PTE's mappings to new PTE */
    pagetable_size = sizeof(old->as_pages)/sizeof(old->as_pages[0]);

    for(i = 0; i < pagetable_size; i++) {
        memmove((void *)PADDR_TO_KVADDR(new->as_pages[i]),
                (const void *)PADDR_TO_KVADDR(old->as_pages[i]),
                PAGE_SIZE);

        new->as_pages[i].pte_permissions = old->as_pages[i].pte_permissions;
        new->as_pages[i].pte_vaddr = old->as_pages[i].pte_vaddr;
        new->as_pages[i].pte_paddr = old->as_pages[i].pte_paddr;
    }

	*ret = new;
	return 0;
}

/*
 * Destroy the provided address space.
 */
void
as_destroy(struct addrspace *as)
{
    /* Free up the region list. */
    kfree(as->region_list);
    
    /* Free up the page table entries. */ 
    pagetable_destroy(as->as_pages);

    /* Free up the address space itself. */
    kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */

	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
}

/*
 * Takes in an addrspace struct and gets physical pages for each region.
 * DUMBVM previously only mapped the first page of each block of allocated 
 * memory. 
 * We change that so that each virtual page is mapped to a physical frame.
 * Returns: 0 if successful
 *          errno otherwise
 */
int
as_prepare_load(struct addrspace *as)
{
    struct region *regionlist = as->regionlist;
    struct pagetable_entry *pages;
    vaddr_t vaddr;
    paddr_t paddr;
    int region_size = sizeof(regionlist)/sizeof(regionlist.[0]);
    int i, j=0, k=0;
    size_t numPages = 0;

    /* Get number of pages from number of regions * size of each region. */
    for(i = 0; i < region_size; i++) {
        numPages += as->regionlist[i].as_npages;
    }

    /*
     * Allocate physical pages for each region, and save the mapping to our
     * page table.
     */
    as->pages = (struct pagetable_entry*)
                kmalloc(numPages * sizeof(struct pagetable_entry));
    for(i=0; i<region_size; i++) {
    
        /* Setup page table entries for this region. */
        vaddr_t = regionlist[i].as_vbase;
        
        /* Create a new page table entry. */
        as->pages[k].pte_vaddr = vaddr;
        paddr = alloc_kpages(1);
        if(paddr == 0) {
            return ENOMEM;
        }
        as->pages[k].paddr = paddr;

        k++;        
        vaddr += PAGE_SIZE;
        
        /* Populate subsequent page table entries. */
        for(j=0; j<regionlist[i].as_npages; j++) {
        
            as->pages[k].pte_vaddr = vaddr;
            paddr = alloc_kpages(1);
            if(paddr == 0) {
                return ENOMEM;
            }
            as->pages[k].pte_paddr = paddr;
        
            vaddr += PAGE_SIZE;
            k++;
        }
    }
    
    /* Allocate physical pages for stack and heap. */
    //TODO: REPLACE DUMBVM WAY OF DOING IT
	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

/*
 * Initialize a new page table and return it.
 */
struct pagetable_entry *
pagetable_create(void) {
    struct pagetable_entry *pt = (struct pagetable_entry *) 
                                    kmalloc(sizeof(struct pagetable_entry));
    
    KASSERT(newpt != NULL);
    return pt;
}

/*
 * Desetroy the provided page table.
 */
void
pagetable_destroy(struct pagetable_entry *pt) {
    int pagetable_size, i;

    /* Free up each page table entry one by one. */
    pagetable_size = sizeof(pt)/sizeof(pt[0]);
    for(i = 0; i < pagetable_size; i++) {
        kfree(pt[i]);
    }
}

/*
 * Find the provided address spaces's page entry specified by vaddr.
 */
struct pagetable_entry *
get_pagetable_entry(struct addrspace *as, vaddr_t vaddr) {
    int pagetable_size, i;

    /* Search for the pagetable_entry with vaddr. */
    pagetable_size = sizeof(as->as_pages)/sizeof(as->as_pages[0]);
    for(i = 0; i < pagetable_size; i++) {
        if(as->as_pages[i].vaddr == vaddr) {
            return &as->as_pages[i];
        }
    }

    return NULL;
}
