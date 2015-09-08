#include "kernel.h"

void create_sem(Sem *s, int token) {
   s->token = token;
   list_init(&s->block);
}

void P(Sem *s) {
   lock();
   s->token--;
   if(s->token < 0) {
      list_add_before(&(s->block), &(current->sem));
      sleep();
   }
   unlock();
}

int num = 0;

void V(Sem *s) {
   lock();
   s->token++;
   if(s->token <= 0) {
      assert(!list_empty(&s->block));
      ListHead *p = s->block.next;
      list_del(p);
      wakeup(list_entry(p, PCB, sem));   
   }
   unlock();
}