#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>

/* Place your frametable data-structures here
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */

typedef struct frame_table_entry {
    int next_free;
}fte;

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock free_mem_lock = SPINLOCK_INITIALIZER;

fte *frame_table = 0;
static int first_free_entry;

void ft_initialise(){
    paddr_t top_of_ram = ram_getsize();
    unsigned int nframes = top_of_ram / PAGE_SIZE;
    paddr_t location = top_of_ram - nframes * sizeof(fte);
    frame_table = (fte *) PADDR_TO_KVADDR(location);

    unsigned int counter;
    paddr_t firstfree = ram_getfirstfree();
    //kprintf("hi hi hi %d\n", firstfree);
    //kprintf("hi hi hi \n");
    for (counter = 0; counter < nframes; counter++){
        //kprintf("hi hi hi1 \n");
        if (counter == nframes - 1){
            frame_table[counter].next_free = -1;
            //kprintf("hi hi hi2 \n");
        }else{
            //kprintf("hi hi hi3 \n");
            //kprintf("hi counter is %d\n", counter);
            frame_table[counter].next_free = counter + 1;
            //kprintf("hi hi hi4 \n");
        }
        //kprintf("hi hi hi5 \n");
    }
    //kprintf("hi hi hi6 %d\n", firstfree);
    for (counter = 0; counter < (firstfree >> 12); counter ++){
        frame_table[counter].next_free = 0;
    }
    first_free_entry = counter;
    kprintf("hi hi hi %d\n", first_free_entry);
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

        paddr_t addr;
        //kprintf("%d", first_free_entry);
        spinlock_acquire(&stealmem_lock);
        if (frame_table == 0){
            addr = ram_stealmem(npages);
        }else{
            if (npages != 1){
                spinlock_release(&stealmem_lock);
                return 0;
            }
            if (first_free_entry == -1){
                spinlock_release(&stealmem_lock);
                return 0;
            }
            addr = first_free_entry << 12;
            int temp = first_free_entry;
            first_free_entry = frame_table[first_free_entry].next_free;
            frame_table[temp].next_free = 0;
        }

        //addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);

        if(addr == 0)
                return 0;

        return PADDR_TO_KVADDR(addr);
}

void free_kpages(vaddr_t addr)
{
        paddr_t paddr = KVADDR_TO_PADDR(addr);
        int index = paddr >> 12;
        spinlock_acquire(&free_mem_lock);
        frame_table[index].next_free = first_free_entry;
        first_free_entry = index;
        spinlock_release(&free_mem_lock);

}
