#include "kernel.h"

#define CREATE_SPACE 1
#define CREATE_CHILD_SPACE 2
#define EXEC_MEMORY 4
#define EXIT_MEMORY 5

#define LOAD_PROCESS 2
#define ANY -1

#define CREATE_PROCESS 1
#define FORK 2
#define EXEC 3
#define EXIT 4
#define WAIT_PID 5

#define EXIT_FILE 13    //when process stoped, send exit_file to file system
#define COPY_FD_TABLE 14
#define INIT_FD_TABLE 15

pid_t PCB_MANAGER;
static void pcb_manager_thread();
static void create_process(Msg *m);
static void create_process_space(pid_t pid, char *file_name);
static void init_fd_table();
static void init_pcb(PCB *pcb);

static void fork(Msg *m);
static void create_child_space(pid_t ppid, pid_t cpid);
static void copy_fd_table(PCB *cpcb, PCB *ppcb);

static void exec(Msg *m);
static int exec_memory_manager(pid_t pid, char *file_name);

static void exit(Msg *m);
static void exit_memory(PCB *pcb);
static void exit_file(PCB *pcb);

static void handle_waitpid(Msg *m);

void init_pcb_manager() {
   PCB *p = create_kthread(pcb_manager_thread);
   PCB_MANAGER = p->pid;
   printk("pcb pid%d\n", PCB_MANAGER);
   wakeup(p);
}

static void pcb_manager_thread() {
   Msg m;
   while(true) {
      receive(ANY, &m);
      switch(m.type) {
         case CREATE_PROCESS:  create_process(&m); break;
         case FORK:  fork(&m); break;
         case EXEC:  exec(&m); break;
         case EXIT:  exit(&m); break;
         case WAIT_PID: handle_waitpid(&m); break;
         default: assert(0);
      }
   }
}

static void create_process(Msg *m) {
   lock();
   PCB *new_pcb = list_entry(free_pcb.list.next, PCB, list);
   //assert(new_pcb->pid <= 27);
   list_del(&new_pcb->list);
   unlock();
    
   char* file_name = m->buf;
   create_process_space(new_pcb->pid, file_name);

   init_fd_table(new_pcb);

   init_pcb(new_pcb);
   wakeup(new_pcb);

   m->dest = m->src;
   m->src = PCB_MANAGER;
   m->ret = new_pcb->pid;
   send(m->dest, m);
}

static void init_fd_table(PCB *pcb) {
   Msg m;
   m.dest = FILE;
   m.src = PCB_MANAGER;
   m.type = INIT_FD_TABLE;
   m.i[0] = pcb->pid;
   send(FILE, &m);
   receive(FILE, &m);
}

static void create_process_space(pid_t pid, char *file_name) {   //create space and load program.
   Msg m;
   m.src = PCB_MANAGER;
   m.type = CREATE_SPACE;
   m.i[0] = pid;
   m.i[1] = (int)file_name;
   send(MEMORY, &m);
   receive(MEMORY, &m);
}

/*static void wrap(void *fun) {

   //((void(*)(int, char**))fun)(argc, argv);
     ((int(*)())fun)();
     ListHead *prev = current->list.prev;
     list_del(&(current->list));
     list_add_before(&(free_pcb.list), &(current->list));
     pre_current = list_entry(prev, PCB, list);

     asm volatile("int  $0x80");
}*/

static void init_pcb(PCB *pcb) {
   lock();
   pcb->is_head = false;
   pcb->is_used = true;
   pcb->lock_count = 0;
   pcb->parent_wait = false;
   pcb->is_kernel = false;

   pcb->current_dir.index = root_dir->index;
   strcpy(pcb->current_dir.filename, root_dir->filename);

   pcb->tf = &(pcb->kstack[KSTACK_SIZE - sizeof(TrapFrame)]);  
   ((TrapFrame*)(pcb->tf))->ebp = 0;
   ((TrapFrame*)(pcb->tf))->ss = SELECTOR_USER(SEG_USER_DATA);
   ((TrapFrame*)(pcb->tf))->esp = (uint32_t)(0xbfffffff);
   ((TrapFrame*)(pcb->tf))->eflags = 0x206;
   ((TrapFrame*)(pcb->tf))->cs = SELECTOR_USER(SEG_USER_CODE);
   ((TrapFrame*)(pcb->tf))->eip = (uint32_t)pcb->entry;   //entry is program entry
    
   init_msg(pcb);
   list_add_before(&(wait_head.list), &(pcb->list));
   unlock();
}



static void fork(Msg *m) {
   lock();
   assert(!list_empty(&free_pcb.list));
   PCB *new_pcb = list_entry(free_pcb.list.next, PCB, list);
   list_del(&new_pcb->list);
   unlock();

   PCB *ppcb = &pcb_pool[m->src];

   int offset = (int)(new_pcb->kstack - ppcb->kstack);
   if(offset > 0) {
      new_pcb->tf = (void*)((uint32_t)m->buf + (uint32_t)offset);      //pointer to first interrupt's tf.
   } else {
      offset = -offset;
      new_pcb->tf = (void*)((uint32_t)m->buf - (uint32_t)offset);      //pointer to first interrupt's tf.
   }
   memcpy(new_pcb->kstack, ppcb->kstack, KSTACK_SIZE);
   ((TrapFrame*)(new_pcb->tf))->eax = 0;         //child process's ret is 0;

   uint32_t *ebp_ptr = &(((TrapFrame*)(new_pcb->tf))->ebp);
   while(*ebp_ptr >= (uint32_t)ppcb->kstack && *ebp_ptr < (uint32_t)ppcb->kstack + KSTACK_SIZE) {       //initial ebp is set zero
      *ebp_ptr += offset;
      ebp_ptr = (uint32_t*)(*ebp_ptr);
   }

   create_child_space(ppcb->pid, new_pcb->pid);        //send msg to memory

   lock();
   new_pcb->lock_count = ppcb->lock_count;
   new_pcb->is_head = false;
   new_pcb->is_used = true;
   new_pcb->is_kernel = false;
   new_pcb->ppid = ppcb->pid;
   new_pcb->parent_wait = false;
   new_pcb->current_dir.index = ppcb->current_dir.index;
   strcpy(new_pcb->current_dir.filename, ppcb->current_dir.filename);

   init_msg(new_pcb);
   list_add_before(&(wait_head.list), &(new_pcb->list));
   unlock();

   copy_fd_table(new_pcb, ppcb);

   m->dest = m->src;
   m->src = PCB_MANAGER;
   m->ret = new_pcb->pid;
   send(m->dest, m);
}

static void copy_fd_table(PCB *cpcb, PCB *ppcb) {
   Msg m;
   m.src = PCB_MANAGER;
   m.type = COPY_FD_TABLE;
   m.i[0] = cpcb->pid;
   m.i[1] = ppcb->pid;
   send(FILE, &m);
   receive(FILE, &m);
}

static void create_child_space(pid_t ppid, pid_t cpid) {
   Msg m;
   m.src = PCB_MANAGER;
   m.type = CREATE_CHILD_SPACE;
   m.i[0] = ppid;
   m.i[1] = cpid;
   send(MEMORY, &m);
   receive(MEMORY, &m);
}

static void exec(Msg *m) {
   PCB *pcb = &pcb_pool[m->src];
   char file_name[256];
   strcpy_to_kernel(pcb, file_name, (char*)m->i[0]);

   char *va_args = (char*)(m->i[1]);            //vitual address of argment string
   char buf[256]  __attribute((aligned(4)));
   char *temp = (char *)buf + 4;           //first 4 byte used for saving string's address.
   strcpy_to_kernel(pcb, temp, va_args);
   int args_len = strlen(temp);

   //need memory_manager alter space do something
   if(exec_memory_manager(pcb->pid, file_name) == -1) {
      m->dest = m->src;
      m->ret = -1;
      m->src = PCB_MANAGER;
      send(m->dest, m);
      return;
   }

   /*set argments in right place*/
   uint32_t stack_base = 0xbfffffff;
   char *new_args = (char*)((stack_base - args_len) ^ 0x3);     //  argment string's new address, align to 4
   *((uint32_t *)(temp - 4)) = (uint32_t)new_args;                           //args's first 4 byte save argment string's new address
   char *stack_head = new_args - 4;                                    //  stack need save new address.
   copy_from_kernel(pcb, stack_head, temp-4, args_len+4 + 1);         //need copy '\0'

   pcb->lock_count = 0;
   pcb->is_kernel = false;
   pcb->tf = &(pcb->kstack[KSTACK_SIZE - sizeof(TrapFrame)]); 
   ((TrapFrame*)(pcb->tf))->esp = (uint32_t)stack_head - 4;         //esp need point to return address
   ((TrapFrame*)(pcb->tf))->ebp = 0;
   ((TrapFrame*)(pcb->tf))->ss = SELECTOR_USER(SEG_USER_DATA);
   ((TrapFrame*)(pcb->tf))->eflags = 0x206;
   ((TrapFrame*)(pcb->tf))->cs = SELECTOR_USER(SEG_USER_CODE);
   ((TrapFrame*)(pcb->tf))->eip = (uint32_t)pcb->entry;   //entry is new program entry

   init_msg(pcb);

   wakeup(pcb);
}

static int exec_memory_manager(pid_t pid, char *file_name) {
   Msg m;
   m.src = PCB_MANAGER;
   m.i[0] = pid;
   m.i[1] = (int)file_name;
   m.type = EXEC_MEMORY;
   send(MEMORY, &m);
   receive(MEMORY, &m);
   return m.ret;             //stack_head, stack has saved argment string.
}

static void exit(Msg *m) {
   PCB *pcb = &pcb_pool[m->src];

   exit_memory(pcb);     //tell memory_manager takeback pages.

   exit_file(pcb);

   lock();
   pcb->is_used = false;
   list_del(&pcb->list);
   list_add_before(&free_pcb.list, &pcb->list);
   unlock();

   if(pcb->parent_wait) {     //if parent pcb call waitpid send msg to wakeup it;
     m->src = PCB_MANAGER;
     m->ret = m->i[0];
     send(pcb->ppid, m);
   }
}

static void exit_file(PCB *pcb) {
    Msg m;
    m.src = PCB_MANAGER;
    m.type = EXIT_FILE;
    m.i[0] = pcb->pid;
    send(FILE, &m);
    receive(FILE, &m);
}

static void exit_memory(PCB *pcb) {
   Msg m;
   m.src = PCB_MANAGER;
   m.type = EXIT_MEMORY;
   m.i[0] = pcb->pid;
   send(MEMORY, &m);
   receive(MEMORY, &m);
}

static void handle_waitpid(Msg *m) {
   PCB *ppcb = &pcb_pool[m->src];
   PCB *cpcb = &pcb_pool[m->i[0]];
   lock();
   if(cpcb->pid >= 0 && cpcb->pid < 50) {
      if(ppcb->pid == cpcb->ppid && cpcb->is_used) {
         cpcb->parent_wait = true;
         return;
      }
   }
    unlock();
   //indicate no valid pid;
   m->dest = m->src;
   m->ret = -1;
   m->src = PCB_MANAGER;
   send(m->dest, m);
}

//usr process in ring0, as kernel process.
/*
static void init_pcb(PCB *pcb) {
    pcb -> is_head = false;
    pcb -> lock_count = 0;
    pcb->tf = &(pcb->kstack[KSTACK_SIZE - sizeof(TrapFrame) - 8 + 8]);     //need change
    ((TrapFrame*)(pcb->tf)) -> ebp = 0;
    ((TrapFrame*)(pcb->tf)) -> esp = (uint32_t)pcb->tf;
    ((TrapFrame*)(pcb->tf)) -> eflags = 0x206;
    ((TrapFrame*)(pcb->tf)) -> cs = (uint32_t)8;
    ((TrapFrame*)(pcb->tf)) -> eip = (uint32_t)wrap;
    *((uint32_t*)(&pcb->kstack[KSTACK_SIZE - 4])) = (uint32_t)pcb->entry;   //entry is program entry
    
    lock();
    init_msg(pcb);
    list_add_before(&(wait_head.list), &(pcb->list));
    unlock();
}*/


/*
static void fork(Msg *m) {
   lock();
   PCB *new_pcb = list_entry(free_pcb.list.next, PCB, list);
   assert(new_pcb->pid <= 27);
   list_del(&new_pcb->list);
     unlock();

     PCB *ppcb = &pcb_pool[m->src];
     new_pcb->lock_count = ppcb->lock_count;
     new_pcb->is_head = false;
     memcpy(new_pcb->kstack, ppcb->kstack, KSTACK_SIZE);

     int offset = (int)(new_pcb->kstack - ppcb->kstack);
     new_pcb->tf = (void*)((uint32_t)m->buf + offset);      //pointer to first tf.

     ((TrapFrame*)(new_pcb->tf))->eax = 0;
     uint32_t *ebp_ptr = &(((TrapFrame*)(new_pcb->tf))->ebp);
     while(*ebp_ptr != 0) {       //initial ebp is set zero
          *ebp_ptr += offset;
          ebp_ptr = (uint32_t*)(*ebp_ptr);
     }
     create_child_space(ppcb->pid, new_pcb->pid);        //send msg to memory

     init_msg(new_pcb);
     list_add_before(&(wait_head.list), &(new_pcb->list));

     m->dest = m->src;
   m->src = PCB_MANAGER;
   m->ret = new_pcb->pid;
   send(m->dest, m);

}
*/