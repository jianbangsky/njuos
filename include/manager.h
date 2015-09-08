#ifndef __MANAGER_H__
#define __MANAGER_H__

extern pid_t FILE;
extern pid_t MEMORY;
extern pid_t PCB_MANAGER;

void init_manager();

void init_file();
void init_memory();
void init_pcb_manager();

void do_read(int file_name, pid_t req_pid, uint8_t *buf, off_t offset, size_t len);     //file_manager supports to read file

#endif