#include "process.h"
#include "string.h"
#include "memory.h"

#define ADDR_CONVERT 3

#define PAGE_SIZE 4096

extern pid_t MEMORY;

void* physical_addr(PCB *pcb, void * vir_addr);
int strlen_user_string(PCB *pcb, char* vir_addr);


void copy_from_kernel(PCB* pcb, void* dest, void* src, int len){
	if(pcb->is_kernel) {
		memcpy(dest, src, len);
		return;
	}
   int temp = PAGE_SIZE - ((uint32_t)dest & 0xfff);
   int read_len = temp < len ? temp : len;
	void *pa_dest = physical_addr(pcb, dest);
	while(len > 0) {
		memcpy(pa_dest, src, read_len);
		dest += read_len;
		src += read_len;
		len -= read_len;
		read_len = PAGE_SIZE < len ? PAGE_SIZE : len;
		pa_dest = physical_addr(pcb, dest);
	}
}

void copy_to_kernel(PCB* pcb, void* dest, void* src, int len){
	if(pcb->is_kernel) {
		memcpy(dest, src, len);
		return;
	}
   int temp = PAGE_SIZE - ((uint32_t)src & 0xfff);
   int read_len = temp < len ? temp : len;
	void *pa_src = physical_addr(pcb, src);

	while(len > 0) {
		memcpy(dest, pa_src, read_len);
		src += read_len;
		dest += read_len;
		len -= read_len;
		read_len = PAGE_SIZE < len ? PAGE_SIZE : len;
		pa_src = physical_addr(pcb, src);
	}
}

void strcpy_to_kernel(PCB* pcb, char* dest, char* src){
	int len = strlen_user_string(pcb, src); //include '\0'
	copy_to_kernel(pcb, dest, src, len);
}

void strcpy_from_kernel(PCB* pcb, char* dest, char* src){
	//dest = physical_addr(pcb->pid, dest);
	int len = strlen(src);
	copy_from_kernel(pcb, dest, src, len);
}

void* physical_addr(PCB *pcb, void * vir_addr) {
	PDE *pdir = (PDE *)va_to_pa(pcb->kpdir);
	uint32_t pdir_idx = (uint32_t)vir_addr >> 22;
	assert(pdir[pdir_idx].present);
	PTE *ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
	uint32_t ptable_idx = ((uint32_t)vir_addr >> 12) & 0x3ff;
	assert(ptable[ptable_idx].present);
	void *pframe = (void *)(ptable[ptable_idx].page_frame << 12);
	void *pa = (void *)((uint32_t)pframe + ((uint32_t)vir_addr & 0xfff));
	return pa;
}

int strlen_user_string(PCB *pcb, char* vir_addr) {
	char *pa_addr = (char *)physical_addr(pcb, vir_addr);
	char *page_end = (char *)(((uint32_t)pa_addr & 0xfffff000) + PAGE_SIZE);
	vir_addr = (char *)(((uint32_t)vir_addr & 0xfff) + PAGE_SIZE);
   int len = 0;
	while(true) {
		while(pa_addr < page_end) {
			if(*pa_addr == '\0') {
				len++;
				return len;
			}
			len++;
			pa_addr++;
		}
		pa_addr = (char *)physical_addr(pcb, vir_addr);
		page_end = pa_addr + PAGE_SIZE;
		vir_addr += PAGE_SIZE;
	}
}
