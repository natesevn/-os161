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

#define DUMBVM_STACKPAGES    18

/* Flag to indicate that bootstrap is complete */
static bool vm_initialized = false;

/* Our coremap array */
struct coremap_entry* coremap;
static unsigned long num_coremap_pages;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

/* paddrs available after coremap allocation */
paddr_t freeaddr;

/* 
 * Initialize coremap
 * When done, memory should look like this: 
 * firstaddr <----- coremap -----> freepaddr
 * freepaddr <----- freemem -----> lastpaddr
 */
void vm_bootstrap(void){
    int num_ppages, i;
    int num_dirty, num_fixed, num_free;
    paddr_t firstaddr, lastaddr;

    /* Initialize RAM and range of addr in RAM  */
    firstaddr = ram_getfirstfree();
    lastaddr = ram_getsize();

    /* Find num of physical pages in system using PAGE_SIZE */
    num_ppages = (lastaddr-firstaddr) / PAGE_SIZE;
    num_coremap_pages = num_ppages;    
    coremap = (struct coremap_entry*)PADDR_TO_KVADDR(firstaddr);

    /*
     * Make sure coremap has been properly allocated 
     * Save starting paddr after coremap 
     */
    freeaddr = firstaddr + num_ppages * sizeof(struct coremap_entry);
    freeaddr = ROUNDUP(freeaddr, PAGE_SIZE);
    
    KASSERT((lastaddr - freeaddr) % PAGE_SIZE == 0);    

    /* Determine which sections of the memory corresponds to what */
    num_dirty = (firstaddr - 0) / PAGE_SIZE;
    num_fixed = (freeaddr - firstaddr) / PAGE_SIZE; 
    num_free = (lastaddr - freeaddr) / PAGE_SIZE;
    KASSERT(num_fixed + num_free == num_ppages);

    /* 
     * Initialize coremap array
     * stolen  pages are dirty,
     * coremap pages are fixed,
     * remaining pages are free.
     */
    for(i=0; i<num_ppages; i++) {
        
        if(i < num_dirty) {
            coremap[i].state = DIRTY;
        }
        else if(i > num_dirty && i < num_fixed) {
            coremap[i].state = FIXED;
        }
        else {
            coremap[i].state = FREE;
        }

        coremap[i].va = PADDR_TO_KVADDR(getPaddr(i));
    }
    
    vm_initialized = true;
}

/*
 * Get the next available physical page(s) and return it.
 * Return: 0 if no pages are available,
 *         else PA of the next available page(s).
 */
paddr_t getppages(unsigned npages) {
    paddr_t first_page;
    unsigned long page_start = 0;
    unsigned long num_pages = 0;
    unsigned long i = 0;
    
    if(vm_initialized) {
        spinlock_acquire(&coremap_lock);
        
        /* Determine first page of a continuous available block. */
        while(num_pages != npages && i < num_coremap_pages) {

            if(coremap[i].state == FREE) {
                num_pages++;
            }
            else {
                num_pages = 0;
                page_start = i+1;
            }

            i++;
        }

        /* Return 0 if there are no available pages. */
        if(i == num_coremap_pages) {
            spinlock_release(&coremap_lock);
            return 0;
        }

        /* Set the various fields of the available block of pages. */
        coremap[page_start].page_start = true;
        coremap[page_start].state = DIRTY;
        coremap[page_start].block_size = npages;
        for(i=1; i<npages; i++) {
        
            coremap[i+page_start].page_start = false;
            coremap[i+page_start].state = DIRTY;
            coremap[i+page_start].block_size = npages;  

        }
    
        /* Return PA of the next available page(s). */
        first_page = getPaddr(page_start);        

    }
    else {
        spinlock_acquire(&coremap_lock);
        first_page = ram_stealmem(npages);
    }
    
    spinlock_release(&coremap_lock);
    return first_page;
}

/* 
 * Allocate continuous FREE pages.
 * Pages should be DIRTY after being allocated.
 * Return: the VA of the first page of the allocated block.
 */
vaddr_t alloc_kpages(unsigned npages) {
    paddr_t first_page = getppages(npages);
    if(first_page == 0) {
        return 0;
    }

    return PADDR_TO_KVADDR(first_page);
}

/*
 * Free continuous pages.
 * Pages should be FREE after being freed.
 */
void free_kpages(vaddr_t addr) {

    unsigned long i;
    unsigned j, block_size;

    spinlock_acquire(&coremap_lock);

    for(i=0; i<num_coremap_pages; i++) {
    
        /* Look for the page corresponding to the VA */
        if(coremap[i].va == addr) {
            KASSERT(coremap[i].page_start == true); 
            KASSERT(coremap[i].block_size > 0);
            KASSERT(coremap[i].state != FIXED);
            break;
        }
    }

    /* 
     * Clear up all pages that are part of the continuous block.
     * "i" now holds the index of the starting page of the block.
     */
    block_size = coremap[i].block_size;

    for(j=0; j<block_size; j++) {

        coremap[j+i].page_start = false;
        coremap[j+i].state = FREE;
        coremap[j+i].block_size = 0;
        
    }

    spinlock_release(&coremap_lock);
}

//TODO
void
vm_tlbshootdown_all(void)
{
    panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
    panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
    paddr_t paddr;
    int i;
    uint32_t ehi, elo;
    struct addrspace *as;
    int spl;

    faultaddress &= PAGE_FRAME;

    DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

    switch (faulttype) {
        case VM_FAULT_READONLY:
        /* We always create pages read-write, so we can't get this */
        panic("dumbvm: got VM_FAULT_READONLY\n");
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
        break;
        default:
        return EINVAL;
    }

    if (curproc == NULL) {
        /*
         * No process. This is probably a kernel fault early
         * in boot. Return EFAULT so as to panic instead of
         * getting into an infinite faulting loop.
         */
        return EFAULT;
    }

    as = proc_getas();
    if (as == NULL) {
        /*
         * No address space set up. This is probably also a
         * kernel fault early in boot.
         */
        return EFAULT;
    }

    /* Assert that the address space has been set up properly. */
    KASSERT(as->as_vbase1 != 0);
    KASSERT(as->as_pbase1 != 0);
    KASSERT(as->as_npages1 != 0);
    KASSERT(as->as_vbase2 != 0);
    KASSERT(as->as_pbase2 != 0);
    KASSERT(as->as_npages2 != 0);
    KASSERT(as->as_stackpbase != 0);
    KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
    KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
    KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
    KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
    KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

    vbase1 = as->as_vbase1;
    vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
    vbase2 = as->as_vbase2;
    vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
    stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
    stacktop = USERSTACK;

    if (faultaddress >= vbase1 && faultaddress < vtop1) {
        paddr = (faultaddress - vbase1) + as->as_pbase1;
    }
    else if (faultaddress >= vbase2 && faultaddress < vtop2) {
        paddr = (faultaddress - vbase2) + as->as_pbase2;
    }
    else if (faultaddress >= stackbase && faultaddress < stacktop) {
        paddr = (faultaddress - stackbase) + as->as_stackpbase;
    }
    else {
        return EFAULT;
    }

    /* make sure it's page-aligned */
    KASSERT((paddr & PAGE_FRAME) == paddr);

    /* Disable interrupts on this CPU while frobbing the TLB. */
    spl = splhigh();

    for (i=0; i<NUM_TLB; i++) {
        tlb_read(&ehi, &elo, i);
        if (elo & TLBLO_VALID) {
            continue;
        }
        ehi = faultaddress;
        elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
        DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
        tlb_write(ehi, elo, i);
        splx(spl);
        return 0;
    }

    kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
    splx(spl);
    return EFAULT;
}

/* Get coremap index number given a physical address. */
unsigned long getIndex(paddr_t page_addr) {
    unsigned long index = (page_addr - freeaddr) / PAGE_SIZE;
    return index;
}

/* Get physical address of page referenced by given coremap index number. */
paddr_t getPaddr(unsigned long index) {
    paddr_t pAddr = index * PAGE_SIZE + freeaddr;
    return pAddr;
}





