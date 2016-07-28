#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <synch.h>

/* Place your frametable data-structures here 
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */

/* Frame table entry structure, contains the information about the
 * state of the frame (used or not), the next, and previous free frame 
 * inside the frame table.
 */
struct frame_table_entry {
    bool used;
    int next;
    int prev;
};
  
/* Index of first free frame in the frame table */
static int first_free;

struct frame_table_entry *frametable = NULL;

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/* Frametable is shared between processes, hence it needs a lock.
 * Spinlock make sure it can be obtained during context switch.
 */
static struct spinlock frametable_lock = SPINLOCK_INITIALIZER;

/* Frametable initialisation function, called from vm_bootstrap */	
void frametable_init() {
    /* Allocate the frame table at the top of the RAM */
    paddr_t top_of_ram = ram_getsize();
    /* Number of frames is the size of RAM divided by size of page */
    unsigned int nframes = top_of_ram/PAGE_SIZE;
    paddr_t location = top_of_ram - (nframes * sizeof(struct frame_table_entry)); 
    frametable = (struct frame_table_entry *) PADDR_TO_KVADDR(location);
    
    /* First free frame after OS161 bootstraps */
    paddr_t firstfree = ram_getfirstfree();
    
    /* Create frame table entries for all frames */
    struct frame_table_entry new;
    unsigned int i;
    for (i = 0; i < nframes; i++) {
        new.used = false;
        if (i != nframes-1) {
            new.next = i+1;
        } else {
            new.next = 0;
        }
        if (i != 0) {
            new.prev = i-1;
        } else {
            new.prev = nframes-1;
        }
        memmove(&frametable[i], &new, sizeof(new));
    }
    
    /* Mark the frames used for kernel */
    for (i = 0; i <= firstfree >> 12; i++) {
        frame_remove(i);
    }
    frame_remove(i);
    first_free = i+1;
    /* Mark the frames used for frame table */
    for (i = nframes-1; i >= location >> 12; i--) {
        frame_remove(i);
    }
}


/* Note that this function returns a VIRTUAL address, not a physical 
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */

vaddr_t alloc_kpages(unsigned int npages)
{
	/* Assume we only support 1 page allocation at a time */
	if (npages != 1) {
	    return 0;
	}
    
    paddr_t addr = 0;
    
    /* Avoid race condition on frametable. */
    spinlock_acquire(&frametable_lock);
    
    /* first_free = -1 indicates no more free frame. */
    if (first_free == -1) {
        spinlock_release(&frametable_lock);
        return 0; 
    }
    
    /* If frametable has yet to be initialised, 
     * use the bump pointer allocator. 
     */	
    if (frametable == NULL) {
        spinlock_acquire(&stealmem_lock);
	    addr = ram_stealmem(npages);
	    spinlock_release(&stealmem_lock);
	} else {
	    addr = first_free << 12;
        frame_remove(first_free);
        /* If this is the last free frame, make the first_free invalid for next allocation */
        if (frametable[first_free].next == first_free) {
            first_free = -1;
        } else {
            first_free = frametable[first_free].next;
        }
	}
	spinlock_release(&frametable_lock);
	if (addr == 0) {
		return 0;
    }
    /* Zero-fill the frame to be allocated */
    bzero((void *)PADDR_TO_KVADDR(addr), PAGE_SIZE);

    /* Function returns the virtual address of the frame */
    return PADDR_TO_KVADDR(addr);
} 

/* Function to free allocated pages, called by kfree.
 * By freeing a frame, it returns said frame to the 
 * free list.
 */
void free_kpages(vaddr_t addr)
{
	paddr_t paddr = KVADDR_TO_PADDR(addr);
	/* Shifts the physical address right by 12 times,
	 * getting the index of the frame table. Using this,
     * we avoid having to loop through all the frames.
     */
    int index = paddr >> 12;
	spinlock_acquire(&frametable_lock);
        
    /* If the frame is not allocated, release the lock
     * and do nothing.
     */
	if (!frametable[index].used) {
		spinlock_release(&frametable_lock);
		return;
	}

    /* Mark the frame as unused */
	frametable[index].used = false;

    /* If there are no other free frames in the free list,
     * make this frame the only frame in the list by assigning
     * its own index as the next and the previous in the list.
     */
	if (first_free == -1) {
	    frametable[index].prev = frametable[index].next = index;
    /* Else make the frame the head of the free list. */
	} else {
	    frametable[index].prev = frametable[first_free].prev;
	    frametable[first_free].prev = index;
	    frametable[index].next = first_free;
	}

    /* The freed frame becomes the first free frame to be allocated. */
	first_free = index;
	spinlock_release(&frametable_lock);
}

/* Helper function to remove frame from free list */
void frame_remove(int i) {
    frametable[i].used = true;
    frametable[frametable[i].prev].next = frametable[i].next;
    frametable[frametable[i].next].prev = frametable[i].prev;
}
