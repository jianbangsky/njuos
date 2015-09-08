#include "../../../include/sys_call.h"

int __attribute__((__noinline__))
syscall(int id, ...) ;


void puts(const char* s) {
	syscall(SYS_puts, s);
}

void puts1(const char* s) {
	syscall(SYS_puts1, s);
}

pid_t fork(void) {
	pid_t pid = syscall(SYS_fork);
	return pid;
}

int exec(char* file_name, char* args) {
	return syscall(SYS_exec, file_name, args);
}

void exit(int exit_no) {
	syscall(SYS_exit, exit_no);
}

pid_t getpid() {
	return syscall(SYS_getpid);
}

int waitpid(pid_t pid) {
	return syscall(SYS_waitpid, pid);
}

void read_line(char *cmd) {
	syscall(SYS_read_line, cmd);
}

void sys_sleep(int time) {
	syscall(SYS_sleep, time);
}

int open(const char* pathname, int oflag, int mode) {
	return syscall(SYS_open, pathname, oflag, mode);
}

int read(int fd, uint8_t *buf, int len) {
	return syscall(SYS_read, fd, buf, len);
}

int write(int fd, uint8_t *buf, int len) {
	return syscall(SYS_write, fd, buf, len);
}

int create(const char* pathname, int mode) {
	return syscall(SYS_create, pathname, mode);
}

int close(int fd) {
	return syscall(SYS_close, fd);
}

int delete_file(const char* pathname) {
	return syscall(SYS_delete, pathname);
}

off_t lseek(int fd, off_t offset, int whence) {
	return syscall(SYS_lseek, fd, offset, whence);
}

int dup(int fd) {
	return syscall(SYS_dup, fd);
}

int dup2(int fd1, int fd2) {
	return syscall(SYS_dup2, fd1, fd2);
}

int mkdir(char *dir) {
	return syscall(SYS_mkdir, dir);
}

int rmdir(char *dir) {
	return syscall(SYS_rmdir, dir);
}

int lsdir(char *dir, char *buf) {
	return syscall(SYS_lsdir, dir, buf);
}

int chdir(char *dir) {
	return syscall(SYS_chdir, dir);
}



int __attribute__((__noinline__))
syscall(int id, ...) {
	int ret = 0;
	int *args = &id;
	asm volatile("int $0x80": "=a"(ret) : "a"(args[0]), "b"(args[1]), "c"(args[2]), "d"(args[3]));
	return ret;
}

