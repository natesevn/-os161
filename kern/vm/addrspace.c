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

/*static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}*/

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
	as->as_pages = NULL;

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
    if(new->regionlist == NULL) {
        return ENOMEM;
    }
    
    for(i = 0; i < region_size; i++) {
        new->regionlist[i].as_vbase = old->regionlist[i].as_vbase;
        new->regionlist[i].as_pbase = old->regionlist[i].as_pbase;
        new->regionlist[i].as_npages = old->regionlist[i].as_npages;
        new->regionlist[i].permissions = old->regionlist[i].permissions;
    }
    
	/* Allocate physical pages for the new addrspace. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

    /* Copy old PTE's mappings to new PTE */
    pagetable_size = sizeof(old->as_pages)/sizeof(old->as_pages[0]);

    for(i = 0; i < pagetable_size; i++) {
        memmove((void *)PADDR_TO_KVADDR(new->as_pages[i].pte_paddr),
                (const void *)PADDR_TO_KVADDR(old->as_pages[i].pte_paddr),
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
    kfree(as->regionlist);
    
    /* Free up the page table entries. */ 
    pagetable_destroy(as->as_pages);

   /* Free up the address space itself. */
    kfree(as);
}

/*
 * Perform TLB shootdown on the current address space.
 */
void
as_activate(void)
{
	/* Disable interrupts on this CPU and shoot down the tlb entries. */
	int spl = splhigh();
    //vm_tlbshootdown_all();
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
    struct region *regionlist;
	size_t npages;
    int permissions;
    int regionListSize;
    int i;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

    /* Determine desired permissions. */
    permissions = 7 & (readable | writeable | executable);

    /* Get size of regionlist array. */
    regionListSize = sizeof(as->regionlist)/sizeof(as->regionlist[0]);
    
    /* Dynamically resize regionlist array by 2 if needed. */
    if(as->regionlist[regionListSize-1].as_vbase > 0) {
        
        /* Copy data from old regionlist to new regionlist. */
        regionlist = (struct region*)
                     kmalloc((regionListSize + 2) * sizeof(struct region));    
        if(regionlist == NULL) {
            return ENOMEM;
        }

        for(i=0; i<regionListSize; i++) {
            regionlist[i].as_vbase = as->regionlist[i].as_vbase;
            regionlist[i].as_pbase = as->regionlist[i].as_pbase;
            regionlist[i].as_npages = as->regionlist[i].as_npages;
            regionlist[i].permissions = as->regionlist[i].permissions;
        }
        
        /* Initialize value of last two additional regions in regionlist. */
        for(i=0; i<2; i++) {
            regionlist[regionListSize+i].as_vbase = 0;
            regionlist[regionListSize+i].as_pbase = 0;
            regionlist[regionListSize+i].as_npages = 0;
            regionlist[regionListSize+i].permissions = 0;
        }
        regionListSize += 2;

        /* Free old regionlist. */
        kfree(as->regionlist);
        as->regionlist = regionlist;
    }        

    /* Setup new region */
    as->regionlist[regionListSize -2].as_vbase = vaddr;
    as->regionlist[regionListSize -2].as_pbase = 0;
    as->regionlist[regionListSize -2].as_npages = npages;
    as->regionlist[regionListSize -2].permissions = permissions;

	return 0;
}

/*
 * Takes in an addrspace struct and gets physical pages for each region.
 * DUMBVM previously only mapped the first page of each block of allocated 
 * memory. 
 * We change that so that each virtual page is mapped to a physical frame.
 * Additionally, change the permissions of each page to read/write.
 * Returns: 0 if successful
 *          errno otherwise
 */
int
as_prepare_load(struct addrspace *as)
{
    struct region *regionlist = as->regionlist;
    vaddr_t vaddr, stackvaddr;
    paddr_t paddr, stackpaddr;
    size_t region_size = sizeof(regionlist)/sizeof(regionlist[0]);
    size_t i=0, j=0, k=0;
    size_t temp = 0;
    size_t numPages = 0;
    size_t ptSize = 0;
    
    /* 
     * Change permissions to read/write in as_prepare_load. Return them to
     * original values in as_complete_load which is stored in struct region.
     */
    int permissions = 7 & ( 100 | 010 | 000 );

    /* Get number of pages from number of regions * size of each region. */
    for(i = 0; i < region_size; i++) {
        numPages += as->regionlist[i].as_npages;
    }
    
    /* Round up numPages to the next power of two. */
    if(!isPowerTwo(numPages)) {
        temp = getPowerTwo(numPages);
        KASSERT(temp > numPages);
        numPages = temp;
    }

    /*
     * Allocate physical pages for each region, and save the mapping to our
     * page table. We allocate at powers of two at a time.
     */
    
    /* Create a new page table with INITIAL_SIZE if it does not exist. */
    if(as->as_pages == NULL) {
        as->as_pages = pagetable_create(INITIAL_SIZE);
        if(as->as_pages == NULL) {
            return ENOMEM;
        }

        ptSize = 64;
    }
    else {
        /*
         * Get size of current table and figure on which index is the last
         * page table entry is. 
         */
        ptSize = sizeof(as->as_pages)/sizeof(as->as_pages[0]);
        while(as->as_pages[i].pte_vaddr != 0) {
            i++;
        }
    }
    
    /* If the table is not big enough to hold the data or there aren't enough
       empty spaces in the table, resize. */
    if(numPages > ptSize || numPages > ptSize - i) {
        as->as_pages = pagetable_resize(as->as_pages, ptSize);
    }
    
    /* Start filling in the page table from the index of the last entry. */
    k = i;
    for(i=0; i<region_size; i++) {
    
        /* Setup page table entries for this region. */
        vaddr = regionlist[i].as_vbase;
        
        /* Create a new page table entry. */
        as->as_pages[k].pte_vaddr = vaddr;
        paddr = getppages(1);
        if(paddr == 0) {
            return ENOMEM;
        }
        as->as_pages[k].pte_paddr = paddr;
        as->as_pages[k].pte_permissions = permissions;

        k++;        
        vaddr += PAGE_SIZE;
        
        /* Populate subsequent page table entries. */
        for(j=0; j<regionlist[i].as_npages; j++) {
        
            as->as_pages[k].pte_vaddr = vaddr;
            paddr = alloc_kpages(1);
            if(paddr == 0) {
                return ENOMEM;
            }
            as->as_pages[k].pte_paddr = paddr;
            as->as_pages[k].pte_permissions = permissions;

            vaddr += PAGE_SIZE;
            k++;
        }
    }
    
    /* Allocate physical pages for stack and heap. */
    /* Get top of stack pointer. */
    as_define_stack(as, &stackvaddr);
    
    /* Set up new page for start of stack. */
    as->as_pages[k].pte_vaddr = stackvaddr;
    stackpaddr = getppages(1);
    if(stackpaddr == 0) {
        return ENOMEM;
    }
    k++;
    
    as->as_stack_start = as->as_stack_end = stackvaddr;
    
    /* Set up new page for start of heap. */
    as->as_pages[k].pte_vaddr = vaddr;
    paddr = getppages(1);
    if(paddr == 0) {
        return ENOMEM;
    }
    k++;

    as->as_heap_start = as->as_heap_end = vaddr; 

	return 0;
}

/*
 * Change all permissions back to its original one (before prepare load changed
 * them);
 */
int
as_complete_load(struct addrspace *as)
{
    struct region *regionlist = as->regionlist;
    size_t regionSize = sizeof(regionlist)/sizeof(regionlist[0]);
    size_t i=0, j=0, k=0;
    size_t npages;

    for(i=0; i<regionSize; i++) {    
        npages = regionlist[i].as_npages;
        k=i+j;
        for(j=0; j<npages; j++) {
            as->as_pages[k].pte_permissions = regionlist[i].permissions;
        }
    }
    
	return 0;
}

/*
 * Returns the USERSTACK inside stackptr
 */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    (void)as;
	*stackptr = USERSTACK;
	return 0;
}

/*
 * Initialize a new page table and return it.
 */
struct pagetable_entry *
pagetable_create(size_t size) {
    struct pagetable_entry *pt = (struct pagetable_entry *) 
                                 kmalloc(size*sizeof(struct pagetable_entry));
    
    KASSERT(pt != NULL);
    return pt;
}

/*
 * Destroy the provided page table.
 */
void
pagetable_destroy(struct pagetable_entry *pt) {
    int pagetable_size, i;

    /* Free up each page table entry one by one. */
    pagetable_size = sizeof(pt)/sizeof(pt[0]);
    for(i = 0; i < pagetable_size; i++) {
        kfree(&pt[i]);
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
        if(as->as_pages[i].pte_vaddr == vaddr) {
            return &as->as_pages[i];
        }
    }

    return NULL;
}

/*
 * Resize the passed in pagetable. The new page table will have a size equal to
 * double the old size.
 */
struct pagetable_entry* 
pagetable_resize(struct pagetable_entry *pt, size_t prevSize) {
    size_t newSize = prevSize*2;
    size_t i;
    
    struct pagetable_entry *newPt = pagetable_create(newSize);

    for(i=0; i<prevSize; i++){
        newPt[i].pte_permissions = pt[i].pte_permissions;
        newPt[i].pte_vaddr = pt[i].pte_vaddr;
        newPt[i].pte_paddr = pt[i].pte_paddr;
    }

    for(i=prevSize; i<newSize; i++){
        newPt[i].pte_permissions = 0;
        newPt[i].pte_vaddr = 0;
        newPt[i].pte_paddr = 0;
    }

    return newPt;
}

/*
 * Check if the number is a power of two.
 * Returns: 1 if true
 *          0 if false
 */
int 
isPowerTwo(size_t num) {
    int testNum = num&(num-1);
    
    if(testNum==0) {
        return 1;
    }
    
    return 0;
}

/*
 * Gets the closest (upper) power of two.
 * Returns: next power of two
 */
size_t 
getPowerTwo(size_t num) {
    size_t power = 2;
    while(num >>= 1) power <<= 1;
    return power;
}
