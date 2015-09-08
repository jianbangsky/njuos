#ifndef __PROCESS_H__
#define __PROCESS_H__

#include "adt/list.h"
#include "x86/cpu.h"
#include "fm.h"

#define KSTACK_SIZE 4096
#define POOL_NUM 50
#define MSG_NUM 100
#define ANY -1

#define NR_PDE 1024
#define NR_PTE 1024
#define PAGE_SIZE 4096

typedef struct Semaphore {
   int token;
   ListHead block;      /* blocking queue */
}Sem;

typedef struct Message {
   pid_t src, dest;
   union {
      int type;
      int ret;
   };
   union {
      int i[5];
      struct {
         pid_t req_pid;
         int dev_id;
         void *buf;
         off_t offset;
         size_t len;
      };
   };
   ListHead list;
} Msg;

typedef struct PCB {
   void *tf;
   uint8_t kstack[KSTACK_SIZE];
   ListHead list;
   ListHead sem;
   bool is_head;
   bool is_used;
   int lock_count;
   pid_t pid;
   pid_t ppid;

   bool parent_wait;

   CR3 cr3;
   PDE kpdir[NR_PDE] align_to_page;                  // process page directory
   //PTE kptable[1024*6] align_to_page;                         // process page tables

   bool is_kernel;      //is kernel thread or user process
   void *entry;

   Msg msg_space[MSG_NUM];
   ListHead free_queue;
   ListHead msg_queue;
   Sem pid_sem[POOL_NUM + 1];     //interrupt has one sem 
   Sem any_sem;

   Open_file_entry* fd_table[100];
   int min_fd;

   dir_entry current_dir;

   uint32_t heap_top;
   uint32_t heap_start;

} PCB;

PCB* create_kthread(void *fun);

void sleep();
void wakeup(PCB *);

void lock();
void unlock();
 
void P(Sem *s);
void V(Sem *s);
void create_sem(Sem *s, int token);

void send(pid_t dest, Msg *m);
void receive(pid_t src, Msg *m);
void init_msg(PCB *pcb);

PCB* fetch_pcb(pid_t pid);

extern PCB *current;
extern PCB *pre_current;
extern PCB idle;
extern PCB wait_head;
extern PCB pcb_pool[POOL_NUM];
extern PCB free_pcb;

#endif
   