#include "kernel.h"
#include "shell.h"
#include "first_program.h"


PCB pcb_pool[POOL_NUM];
PCB wait_head;
PCB free_pcb;

extern CR3 kcr3;

static void wrap(void *fun, int argc, char *argv[]) {

   //((void(*)(int, char**))fun)(argc, argv);
    ((void(*)())fun)();
     
    lock();
    current->is_used = false;
    ListHead *prev = current->list.prev;
    list_del(&(current->list));
    list_add_before(&(free_pcb.list), &(current->list));
    pre_current = list_entry(prev, PCB, list);
    unlock();

    asm volatile("int  $0x80");
}

PCB* fetch_pcb(pid_t pid) {
    assert((pid >= 0) &&(pid < POOL_NUM));
    return &pcb_pool[pid];
}

PCB*
create_kthread_1(void *fun, int argc, char* argv[]) {
    lock();
    PCB *new_pcb = list_entry(free_pcb.list.next, PCB, list);
    
    new_pcb->is_head = false;
    new_pcb->is_used = true;
    new_pcb->lock_count = 0;

    new_pcb->tf = &(new_pcb->kstack[KSTACK_SIZE - sizeof(TrapFrame) - 16 + 8]);      //because esp and ss are needn't current
    ((TrapFrame*)(new_pcb->tf)) -> ebp = 0;      
    ((TrapFrame*)(new_pcb->tf)) -> esp = (uint32_t)new_pcb->tf;
    ((TrapFrame*)(new_pcb->tf)) -> eflags = 0x206;
    ((TrapFrame*)(new_pcb->tf)) -> cs = (uint32_t)8;
    ((TrapFrame*)(new_pcb->tf)) -> eip = (uint32_t)wrap;
    uint32_t *p = (uint32_t*)(&new_pcb->kstack[KSTACK_SIZE - 4]);
    *p = (uint32_t)argv;
    int *q = (int*)(&new_pcb->kstack[KSTACK_SIZE - 8]);
    *q = argc;
    uint32_t* r = (uint32_t*)(&new_pcb->kstack[KSTACK_SIZE - 12]);
    *r = (uint32_t)fun;
    
    new_pcb->cr3 = kcr3;      //all kernel thread has this cr3
    new_pcb->is_kernel = true;

    init_msg(new_pcb);

    list_del(&new_pcb->list);
    list_add_before(&(wait_head.list), &(new_pcb->list));

    unlock();

    return new_pcb;
}


PCB* create_kthread(void *fun) {
    return create_kthread_1(fun, 0, NULL);
}

#define CREATE_PROCESS 1
void write_shell_program_to_file();
void write_first_program_to_file();
void start_shell();


void create_shell() {
    write_shell_program_to_file();
    write_first_program_to_file();
    printk("start_shell\n");
    start_shell();
}

void write_shell_program_to_file() {
    Msg m;
    m.src = current->pid;
    char *shell_name = "/hello/shell";
    m.i[0] = (int)shell_name;
    m.i[1] = 1;
    m.type = OPEN;
    send(FILE, &m);
    receive(FILE, &m);
    int fd = m.ret;

    m.src = current->pid;
    m.i[0] = current->pid;
    m.i[1] = fd;
    m.i[2] = (int)shell;
    m.i[3] = shell_len;
    m.type = WRITE;
    send(FILE, &m);
    receive(FILE, &m);

    m.src = current->pid;
    m.i[0] = fd;
    m.type = CLOSE;
    send(FILE, &m);
    receive(FILE, &m);
}

void write_first_program_to_file() {
    Msg m;
    m.src = current->pid;
    char *shell_name = "/hello/first_program";
    m.i[0] = (int)shell_name;
    m.i[1] = 1;
    m.type = OPEN;
    send(FILE, &m);
    receive(FILE, &m);
    int fd = m.ret;

    m.src = current->pid;
    m.i[0] = current->pid;
    m.i[1] = fd;
    m.i[2] = (int)first_program;
    m.i[3] = first_program_len;
    m.type = WRITE;
    send(FILE, &m);
    receive(FILE, &m);

    m.src = current->pid;
    m.i[0] = fd;
    m.type = CLOSE;
    send(FILE, &m);
    receive(FILE, &m);
}

void start_shell() {
    Msg m;
    m.src = current->pid;
    m.type = CREATE_PROCESS;
    char *shell_name = "/hello/shell";
    m.buf = shell_name;
    send(PCB_MANAGER, &m);
    receive(PCB_MANAGER, &m);
}

void init_proc() {

    current -> pid = -5;
    current -> cr3 = kcr3;

    idle.is_head = true;
    list_init(&(idle.list));
    list_init(&(wait_head.list));
    list_init(&(free_pcb.list));
    int i;
    for(i = 0; i < POOL_NUM; i++) {
        pcb_pool[i].pid = i;
        list_add_before(&free_pcb.list, &pcb_pool[i].list);
    }

    init_driver();
    init_file();
    init_manager();

    wakeup(create_kthread(create_shell));
}
/*
int test_pid;

void read_MBR() {
    uint8_t buffer[512];
    int nread = dev_read("hda", test_pid, buffer, 0, 512);
    assert(nread == 512);
    char out[2000];
    int i, count = 0;
    for(i = 0; i < 512; i++) {
        int h = buffer[i]/16;
        if(h < 10){
            out[count++] = h + '0';
        } else {
            out[count++] = h + 'a' - 10;
        }
        h = buffer[i]%16;
        if(h < 10) {
            out[count++] = h + '0';
        } else {
            out[count++] = h + 'a' - 10;
        }

        if(!((i+1) % 16)) {
            out[count++] = '\n';
        } else {
            out[count++] = ' ';
        }
    }
    dev_write("tty1", test_pid, out, 0, count);
}




#define NEW_TIMER -1
extern pid_t TIMER;

pid_t pid1, pid2, pid3;
void time1() {
    Msg m;
    while(1) {
        m.src = pid1;
        m.type = NEW_TIMER;
        m.i[0] = 1;
        send(TIMER, &m);
        receive(TIMER, &m);
        char* buf = "I'm time1  ";
        dev_write("tty1", pid1 ,buf , 0, 11);

    }
}

void time2() {
    Msg m;
    while(1) {
        m.src = pid2;
        m.type = NEW_TIMER;
        m.i[0] = 2;
        send(TIMER, &m);
        receive(TIMER, &m);
        char* buf = "I'm time2  ";
        dev_write("tty1", pid2, buf , 0, 11);
    }
}

void time3() {
    Msg m;
    while(1) {
        m.src = pid3;
        m.type = NEW_TIMER;
        m.i[0] = 3;
        send(TIMER, &m);
        receive(TIMER, &m);
        char* buf = "I'm time3  ";
        dev_write("tty1", pid3, buf , 0, 11);
    }
}
*/

