#ifndef __SYS__CALL_INTERRUPT_H__
#define __SYS__CALL_INTERRUPT_H__
#include "process.h"

#define SYS_fork 1
#define SYS_exec 2
#define SYS_exit 3
#define SYS_getpid 4
#define SYS_waitpid 5
#define SYS_puts 6
#define SYS_puts1 30
#define SYS_read_line 9
#define SYS_sleep 10

#define SYS_open 11
#define SYS_read 12
#define SYS_write 13
#define SYS_create 14
#define SYS_close 15
#define SYS_delete 16
#define SYS_lseek 17
#define SYS_dup 18
#define SYS_dup2 19
#define SYS_mkdir 20
#define SYS_rmdir 21
#define SYS_lsdir 22
#define SYS_chdir 23

void do_syscall(TrapFrame *tf);

#endif