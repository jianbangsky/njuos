#ifndef __STRING_H__
#define __STRING_H__

#include "common.h"
#include "process.h"

char *itoa(int);
void memcpy(void *, const void *, size_t);
void memset(void *, uint8_t, size_t);
size_t strlen(const char *);
void strcpy(char *d, const char *s);
int strcmp(const char *s1, const char *s2);

void copy_from_kernel(PCB* pcb, void* dest, void* src, int len);
void copy_to_kernel(PCB* pcb, void* dest, void* src, int len);
void strcpy_to_kernel(PCB* pcb, char* dest, char* src);
void strcpy_from_kernel(PCB* pcb, char* dest, char* src);

#endif
