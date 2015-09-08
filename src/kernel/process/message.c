#include "process.h"

void init_msg(PCB *pcb) {
	int i;
	list_init(&pcb->free_queue);
	list_init(&pcb->msg_queue);
	create_sem(&pcb->any_sem, 0);
	for(i = 0; i < POOL_NUM + 1; i++) {
		create_sem(&pcb->pid_sem[i], 0);
	}
	for(i = 0; i < MSG_NUM; i++) {
		list_add_before(&pcb->free_queue, &(pcb->msg_space[i].list));
	}
}


void send(pid_t dest, Msg *m){
	PCB *pcb = &pcb_pool[dest];
	lock();
	Msg *msg = list_entry(pcb->free_queue.next, Msg, list);
     	list_del(&msg->list);

    	*msg = *m;
    	msg->dest = dest;
    	list_add_before(&pcb->msg_queue, &(msg->list));

    	V(&pcb->any_sem);

    	int src = (m->src == -2) ? POOL_NUM : m->src;   //interrupt send massage
    	V(&pcb->pid_sem[src]);

    	unlock();
    
}

void receive(pid_t src, Msg *m) {
	lock();
	Msg *msg = NULL;
	if(src == -1) {
		P(&current->any_sem);
		msg = list_entry(current->msg_queue.next, Msg, list);
		int real_src = (msg->src == -2) ? POOL_NUM : msg->src;
		P(&(current->pid_sem[real_src]));
	} else {
	     int real_src = (src == -2) ? POOL_NUM : src;	     
		P(&current->pid_sem[real_src]);
		P(&current->any_sem);
		ListHead *ptr;
		list_foreach(ptr, &current->msg_queue) {
			msg = list_entry(ptr, Msg, list);
			if(msg->src == src) {
				break;
			}
		}
	}

	assert(msg != NULL);
	list_del(&msg->list);

	*m = *msg;
     
     	list_add_before(&current->free_queue, &msg->list);
     	unlock();
}
