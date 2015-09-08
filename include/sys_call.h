#ifndef __SYS_CALL_H__
#define __SYS_CALL_H__
#include "types.h"

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


void puts(const char*);

void puts1(const char*);

pid_t fork(void);

int exec(char* file_name, char* args);

void exit(int exit_no);

pid_t getpid();

int waitpid(pid_t pid);

void read_line(char *cmd);

void sys_sleep(int time);

int open(const char* pathname, int oflag, int mode);

int read(int fd, uint8_t *buf, int len);

int write(int fd, uint8_t *buf, int len);

int create(const char* pathname, int mode);

int close(int fd);

int delete_file(const char* pathname);

off_t lseek(int fd, off_t offset, int whence);

int dup(int fd);

int dup2(int fd1, int fd2);

int mkdir(char *dir);

int rmdir(char *dir);

int lsdir(char *dir, char *buf);

int chdir(char *dir);

#endif