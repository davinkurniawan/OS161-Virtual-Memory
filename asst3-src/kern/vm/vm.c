#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <current.h>
#include <proc.h>
#include <copyinout.h>
#include <spl.h>

/* Place your page table functions here */

/* Adds root pagetable entry. */ 
int
vm_add_root_ptentry(paddr_t **ptable, uint32_t index)
{
    ptable[index] = kmalloc(sizeof(paddr_t)*PAGETABLE_SIZE);
    if (ptable[index] == NULL) {
        return ENOMEM;
    }
    int i;
    for (i = 0; i < PAGETABLE_SIZE; i++) {
        ptable[index][i] = 0;
    }
    return 0;
}

/* Adds second level pagetable entry. */
int
vm_add_ptentry(paddr_t **ptable, uint32_t msb, uint32_t lsb, uint32_t dirty)
{
    /* Allocate a new frame for this page. */
    vaddr_t page_alloc = alloc_kpages(1);
    if (page_alloc == 0) {
        return ENOMEM;
    }
    paddr_t phys_page_alloc = KVADDR_TO_PADDR(page_alloc);
        
    /* The pagetable entry is the physical address, dirty bit, and valid bit. */
    ptable[msb][lsb] = (phys_page_alloc & PAGE_FRAME) | dirty | TLBLO_VALID;
    return 0;
}

void 
vm_bootstrap(void)
{
    /* Initialise VM sub-system.  You probably want to initialise your 
     * frame table here as well.
     */
    frametable_init();
}

/* Handles TLB miss by searching the pagetable. If entry
 * does not exist, creates a new page table entry.
 */
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    /* TLB entry high argument. */
    uint32_t entry_hi;
    /* TLB entry low argument. */
    uint32_t entry_lo;
    /* Dirty bit of a pagetable entry. */
    uint32_t dirty;
    /* Physical address part of pagetable entry. */
    paddr_t paddr;
    
    /* Helper flag for checking whether we allocate a new second 
     * level pagetable or just an entry in an already allocated table.
     */
    bool flag = false;
    int result;
    
    /* Current implementation only handle faulttype read and write. */
    switch (faulttype) {
        case VM_FAULT_READONLY:
            return EFAULT;
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        default:
            return EINVAL;
    }
    
    if (curproc == NULL) {
        return EFAULT;
    }
    struct addrspace *cur_as = proc_getas();
    if (cur_as == NULL) {
        return EFAULT;
    }
    paddr_t **pagetable = cur_as->ptable;
    if (pagetable == NULL) {
        return EFAULT;
    }
    
    paddr = KVADDR_TO_PADDR(faultaddress);
    
    /* Root page table index. */
    uint32_t msb = paddr >> 22;
    /* Second page table index. */
    uint32_t lsb = paddr << 10 >> 22;
    
    /* Allocate a new 2nd level pagetable if the root entry is still NULL. */
    if (pagetable[msb] == NULL) {
        result = vm_add_root_ptentry(pagetable, msb);
        if (result) {
            return result;
        }
        flag = true;
    }
    
    /* Allocate a new 2nd level pagetable entry in case we can't find the entry. */
    if (pagetable[msb][lsb] == 0) {      
        struct region *cur = cur_as->regions;
        /* If pagetable entry doesn't exist, check if the address is a valid virtual address inside a region */
        while (cur != NULL) {
            if (faultaddress >= cur->vbase && faultaddress < (cur->vbase + (cur->npages*PAGE_SIZE))) {
 			    /* Set the dirty bit if the region is writeable. */
 			    if (cur->writeable_bit != 0) {
			    	dirty = TLBLO_DIRTY;
			    } else {
				    dirty = 0;
			    }
                break;
            }
            cur = cur->next;
        }
        /* If address is not in region, return bad memory error code. */
		if (cur == NULL) {
            if (flag == true) {
                kfree(pagetable[msb]);
            }
            return EFAULT;
        }
        
        result = vm_add_ptentry(pagetable, msb, lsb, dirty);
        if (result) {
            if (flag == true) {
                kfree(pagetable[msb]);
            }
            return result;
        }
    }	
    
    /* Entry high is the virtual address and ASID (not implemented). */
    entry_hi = faultaddress & PAGE_FRAME;
    /* Entry low is physical frame, dirty bit, and valid bit. */
    entry_lo = pagetable[msb][lsb];
	/* Disable interrupts on this CPU while frobbing the TLB. */
    int spl = splhigh();
    /* Randomly add pagetable entry to the TLB. */
    tlb_random(entry_hi, entry_lo);
	splx(spl);
    return 0;
}

/*
 *
 * SMP-specific functions.  Unused in our configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

/*
 * Helper function to flush the TLB.
 */
void
vm_tlbflush() 
{
    int i;
    /* Flush by writing invalid data to the TLB. */
    for (i = 0; i<NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
}

