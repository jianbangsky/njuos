#include "kernel.h"

PCB idle, *current = &idle;
PCB *pre_current = &idle;
inline void set_tss_esp0(uint32_t esp);

void
schedule(void) {
	/* implement process/thread schedule here */
	current = list_entry(pre_current->list.next, PCB, list);
	if(current -> is_head) {
		current = list_entry(current->list.next, PCB, list);
	}
     	pre_current = current;
     	set_tss_esp0((uint32_t)(current->kstack + KSTACK_SIZE-1));
	write_cr3(&current->cr3);          //switch page register
}
