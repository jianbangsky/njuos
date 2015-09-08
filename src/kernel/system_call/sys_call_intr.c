#include "kernel.h"

#define FORK 2
#define EXEC 3
#define EXIT 4
#define WAIT_PID 5
#define READ_LINE 9    //read from tty

#define NEW_TIMER -1

#define OPEN 0
#define READ 1
#define WRITE 2
#define CLOSE 3
#define CREATE 4
#define DELETE 5
#define LSEEK 6
#define DUP 7
#define DUP2 8
#define MKDIR 9
#define RMDIR 10
#define LSDIR 11
#define CHDIR 12

extern pid_t PCB_MANAGER;
extern pid_t TTY;
extern pid_t TIMER;

void syscall_fork(TrapFrame *tf);
void syscall_exec(TrapFrame *tf);
void syscall_exit(TrapFrame *tf);
void syscall_getpid(TrapFrame *tf);
void syscall_waitpid(TrapFrame *tf);
void syscall_read_line(TrapFrame *tf);
void syscall_puts(TrapFrame *tf);
void syscall_sleep(TrapFrame *tf);

void syscall_open(TrapFrame *tf);
void syscall_read(TrapFrame *tf);
void syscall_write(TrapFrame *tf);
void syscall_create(TrapFrame *tf);
void syscall_close(TrapFrame *tf);
void syscall_delete(TrapFrame *tf);
void syscall_lseek(TrapFrame *tf);
void syscall_dup(TrapFrame *tf);
void syscall_dup2(TrapFrame *tf);

void syscall_mkdir(TrapFrame *tf);
void syscall_rmdir(TrapFrame *tf);
void syscall_lsdir(TrapFrame *tf);
void syscall_chdir(TrapFrame *tf);

void do_syscall(TrapFrame *tf) {
   int id = tf->eax;
   switch(id) {
      case SYS_fork:  syscall_fork(tf); break;
      case SYS_exec:  syscall_exec(tf); break;
      case SYS_exit:  syscall_exit(tf); break;
      case SYS_getpid:  syscall_getpid(tf); break;
      case SYS_waitpid:  syscall_waitpid(tf); break;
      case SYS_puts1:  printk((char*)(tf->ebx)); printk("   %d\n", current->pid); break;
      case SYS_puts: syscall_puts(tf); break;
      case SYS_read_line: syscall_read_line(tf); break;
      case SYS_sleep: syscall_sleep(tf); break;

      case SYS_open: syscall_open(tf); break;
      case SYS_read: syscall_read(tf); break;
      case SYS_write: syscall_write(tf); break;
      case SYS_create: syscall_create(tf); break;
      case SYS_close: syscall_close(tf); break;
      case SYS_delete: syscall_delete(tf); break;
      case SYS_lseek: syscall_lseek(tf); break;
      case SYS_dup: syscall_dup(tf); break;
      case SYS_dup2: syscall_dup2(tf); break;
      case SYS_mkdir: syscall_mkdir(tf); break;
      case SYS_rmdir: syscall_rmdir(tf); break;
      case SYS_lsdir: syscall_lsdir(tf); break;
      case SYS_chdir: syscall_chdir(tf); break;
      //default: panic("Unknown system call type");
   }
}

void syscall_puts(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = WRITE;
   m.i[0] = current->pid;
   m.i[1] = 1;
   m.i[2] = tf->ebx;
   m.i[3] = strlen((char*)tf->ebx);         //we need '\0'
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;   
}

void syscall_fork(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = FORK;
   m.buf = (void*)tf;
   //current->lock_count++;
   send(PCB_MANAGER, &m);
   receive(PCB_MANAGER, &m);
   wakeup(&pcb_pool[m.ret]);
   //current->lock_count--;
   tf->eax = m.ret;
}

void syscall_exec(TrapFrame *tf) {
   /*char buf[256];                   //in user's kernel stack, can be read by other kernel thread
   strcpy(buf, (char *)tf->ecx);*/
   Msg m;
   m.src = current->pid;
   m.type = EXEC;
   m.i[0] = (int)tf->ebx;
   //m.buf = buf;
   m.i[1] = (int)tf->ecx;      //send va_addr, pcb_manager use copy_to_kernel to get copy its data, through va_to_pa by memory_manager.
   send(PCB_MANAGER, &m);
   receive(PCB_MANAGER, &m); //only use for block this process.
   tf->eax = m.ret;       //when exec unsuccess ,we can send ret
}

void syscall_exit(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = EXIT;
   m.i[0] = tf->ebx;
   send(PCB_MANAGER, &m);
   receive(PCB_MANAGER, &m);
}

void syscall_getpid(TrapFrame *tf) {
   tf->eax = current->pid;
}

void syscall_waitpid(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = WAIT_PID;
   m.i[0] = tf->ebx;
   send(PCB_MANAGER, &m);
   receive(PCB_MANAGER, &m);
   tf->eax = m.ret;
}

void syscall_read_line(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = READ;
   m.i[0] = current->pid;
   m.i[1] = 0;
   m.i[2] = tf->ebx;
   m.i[3] = 256;         //max line len;
   send(FILE, &m);
   receive(FILE, &m);
   char *buf = (char*)(m.i[2]);
   buf[m.ret] = '\0';
/*   char *buf = (char *)tf->ebx;
   int nread = dev_read("tty1", current->pid, buf, 0, 256);
   buf[nread] = '\0';*/
}

void syscall_sleep(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = NEW_TIMER;
   m.i[0] = tf->ebx;
   send(TIMER, &m);
   receive(TIMER, &m);
}

void syscall_open(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = OPEN;
   m.i[0] = tf->ebx;
   m.i[1] = tf->ecx;
   m.i[2] = tf->edx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_read(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = READ;
   m.i[0] = current->pid;
   m.i[1] = tf->ebx;
   m.i[2] = tf->ecx;
   m.i[3] = tf->edx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_write(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = WRITE;
   m.i[0] = current->pid;
   m.i[1] = tf->ebx;
   m.i[2] = tf->ecx;
   m.i[3] = tf->edx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_create(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = CREATE;
   m.i[0] = tf->ebx;
   m.i[1] = tf->ecx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;   
}

void syscall_close(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = CLOSE;
   m.i[0] = tf->ebx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;   
}

void syscall_delete(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = DELETE;
   m.i[0] = tf->ebx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_lseek(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = LSEEK;
   m.i[0] = tf->ebx;
   m.i[1] = tf->ecx;
   m.i[2] = tf->edx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_dup(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = DUP;
   m.i[0] = tf->ebx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}
void syscall_dup2(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = DUP2;
   m.i[0] = tf->ebx;
   m.i[1] = tf->ecx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_mkdir(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = MKDIR;
   m.i[0] = tf->ebx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_rmdir(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = RMDIR;
   m.i[0] = tf->ebx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_lsdir(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = LSDIR;
   m.i[0] = tf->ebx;
   m.i[1] = tf->ecx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}

void syscall_chdir(TrapFrame *tf) {
   Msg m;
   m.src = current->pid;
   m.type = CHDIR;
   m.i[0] = tf->ebx;
   send(FILE, &m);
   receive(FILE, &m);
   tf->eax = m.ret;
}