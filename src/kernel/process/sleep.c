#include "kernel.h"

void sleep() {
    lock();

    pre_current = list_entry(current->list.prev, PCB, list);
    list_del(&(current->list));
    list_add_before(&(wait_head.list), &(current->list));
    asm volatile("int $0x80");
    unlock();
}

void wakeup(PCB *p) {
    lock();
    ListHead *ptr;
    list_foreach(ptr, &(wait_head.list)) {
        if(ptr == &(p->list)) {
            list_del(ptr);
            list_add_before(&(current->list), ptr);
            break;
        }
    }
    unlock();
}

void lock() {
    cli();
    current->lock_count++;
}

void unlock() {
    if(current->lock_count > 0) {
        current->lock_count--;
    }
    if(current->lock_count == 0) {
        uint32_t eflags = read_eflags();
        if(eflags & 0x200) {
            sti();
        }
    }
}
