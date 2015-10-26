#include "kernel.h"
#include "boot.h"

#define KERNEL_MEM (16*1024*1024)
#define USER_MEM (4*1024*1024)
#define START_ADDR 0x08048000

#define KSTACK_SIZE 4096

#define ANY -1
#define CREATE_SPACE 1
#define CREATE_CHILD_SPACE 2
#define ADDR_CONVERT 3
#define EXEC_MEMORY 4
#define EXIT_MEMORY 5
#define SBRK 6

pid_t MEMORY;
extern pid_t PCB_MANAGER;

uint8_t page_bitmap[28*1024];
uint32_t bitmap_idx;

static void memory_thread();

static void handle_pcb(Msg *m);
static void load_program(PCB *pcb, int elf_fd);
static void load_segment(PCB* pcb, PDE* pdir, struct ProgramHeader* ph, int elf_fd);
static void create_kernel_dir(PDE *pdir);
static void create_stack_page(PDE *pdir);

static void handle_child(Msg *m);
static void copy_process(PCB *ppcb, PCB *cpcb);

static void takeback_page(PDE *pdir);

static void handle_exec(Msg *m);

static void handle_exit(Msg *m);

static void sbrk(Msg *m);

int open_elf_file(char* file_name);
void close_elf_file(int fd);
void read_elf(int elf_fd, pid_t req_pid, uint8_t* buf, int offset, int size);


static uint32_t get_idle_page() {
   while(page_bitmap[bitmap_idx]) {
      bitmap_idx = (bitmap_idx + 1) % (28*1024);
/*      x++;
      if(!(x % 100000)) printk("stop one get_idle_page%d\n", bitmap_idx);*/
   }
   page_bitmap[bitmap_idx] = 1;
   return KERNEL_MEM + (bitmap_idx)*4096;
}

static void set_page_idle(uint32_t page) {
   int idx = (page - KERNEL_MEM)/4096;
   page_bitmap[idx] = 0;
}


void init_memory() {
   bitmap_idx = 0;
   PCB *p = create_kthread(memory_thread);
   MEMORY = p->pid;
   printk("MEMORY%d\n", MEMORY);
   wakeup(p);
}

static void memory_thread() {
   Msg m;
   while(true) {
      receive(ANY, &m);
      switch(m.type) {
         case CREATE_SPACE : handle_pcb(&m);
            break;
         case CREATE_CHILD_SPACE : handle_child(&m);
            break;
         //case ADDR_CONVERT : convert_physical_addr(&m);
            //break;
         case EXEC_MEMORY : handle_exec(&m);
            break;
         case EXIT_MEMORY : handle_exit(&m);
            break;
         case SBRK : sbrk(&m);
            break;
         default: assert(0);
      }
   }
}


static void sbrk(Msg *m) {
   PCB *pcb = fetch_pcb(m->src);
   int inc = m->i[0];
   int new_heap_top = pcb->heap_top + inc;

   uint32_t pdir_idx, ptable_idx;
   PTE *ptable = NULL;
   uint8_t *pframe;
   PDE *pdir = (PDE *)va_to_pa(pcb->kpdir);
   if(inc > 0) {
      uint32_t new_page_addr = pcb->heap_top;
      pdir_idx = (uint32_t)(new_page_addr >> 22);
      if(!pdir[pdir_idx].present) {
         ptable = (PTE *) get_idle_page();
         memset(ptable, 0, PAGE_SIZE);
         make_pde(&pdir[pdir_idx], ptable);
      }
      ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
      ptable_idx = (uint32_t)((new_page_addr >> 12) & 0x3ff);
      if(!ptable[ptable_idx].present) {
         pframe = (uint8_t *)get_idle_page();
         memset(pframe, 0, PAGE_SIZE);
         make_pte(&ptable[ptable_idx], pframe);
      }
      new_page_addr = new_page_addr - (uint32_t)(new_page_addr & 0xfff) + PAGE_SIZE;
      ptable_idx++;
      while(new_page_addr < new_heap_top) {
         pframe = (uint8_t *)get_idle_page();
            memset(pframe, 0, PAGE_SIZE);
         if(ptable_idx == NR_PTE) {  // Need new ptable.
            pdir_idx++;
            ptable = (PTE*)get_idle_page();
            memset(ptable, 0, PAGE_SIZE);
            make_pde(&pdir[pdir_idx], ptable);
            ptable_idx = 0;
         }
         make_pte(&ptable[ptable_idx], pframe);
         ptable_idx++;         
         new_page_addr += PAGE_SIZE;
      }   
   } else if(inc < 0) {
      uint32_t new_page_addr = pcb->heap_top;
      uint32_t remove_page_addr = new_page_addr & 0xfffff000;
      pdir_idx = (uint32_t)(remove_page_addr >> 22);
      ptable_idx = (uint32_t)((remove_page_addr >> 12) & 0x3ff);
      while(remove_page_addr >= new_heap_top) {
         if(ptable_idx < 0) {
            make_invalid_pde(&pdir[pdir_idx]);
            set_page_idle((uint32_t)ptable);
            pdir_idx++;
            ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
            ptable_idx = 0;
         }
         pframe = (uint8_t*)(ptable[ptable_idx].page_frame << 12);
         make_invalid_pte(&ptable[ptable_idx]);
         set_page_idle((uint32_t)pframe);
         ptable_idx --;
      }
   }

   pcb->heap_top = new_heap_top;

   m->dest = m->src;
   m->src = MEMORY;
   m->ret = new_heap_top;
   send(m->dest, m);
}


static void handle_pcb(Msg *m) {
   PCB *pcb = &pcb_pool[m->i[0]];
   char* file_name = (char*)m->i[1];

   int elf_fd = open_elf_file(file_name);
   load_program(pcb, elf_fd);
   close_elf_file(elf_fd);

   m->dest = m->src;
   m->src = MEMORY;
   send(m->dest, m);
}

static void load_program(PCB *pcb, int elf_fd){
   PDE *pdir = (PDE *)va_to_pa(pcb->kpdir);

   create_kernel_dir(pdir);          //create page_dir for kernel
   create_stack_page(pdir);             //set stack page

   struct ELFHeader *elf;
   struct ProgramHeader *ph, *eph;
   elf = (struct ELFHeader*)0x8000;
   read_elf(elf_fd, current->pid, (uint8_t*)elf, 0, 4096);
   ph = (struct ProgramHeader*)((unsigned char *)elf + elf->phoff);
   eph = ph + elf->phnum;
   for(; ph < eph; ph ++) {
      printk("ph->memsz %d\n", ph->memsz);
      if(ph->memsz) {
         load_segment(pcb, pdir, ph, elf_fd);
      }
      if(ph->type == 1) {
         pcb->heap_start = ph->vaddr + ph->memsz;
         pcb->heap_top = pcb->heap_start;
      }
   }

   pcb->cr3.val = 0;
   pcb->cr3.page_directory_base = ((uint32_t)pdir) >> 12;
   pcb->entry = (void*)elf->entry;
}

static void create_stack_page(PDE *pdir) {
   //set stack page, ten page
   uint8_t* stack = (uint8_t *)0xbffff000;
   uint32_t pdir_idx = (uint32_t)stack >> 22;
   PTE* ptable = (PTE *)get_idle_page();
   memset(ptable, 0, PAGE_SIZE);
   make_pde(&pdir[pdir_idx], ptable);
   int i;
   uint32_t ptable_idx = ((uint32_t)stack >> 12) & 0x3ff;
   for(i = 0; i < 10; i++) {
      uint8_t* pframe = (uint8_t *)get_idle_page();
      make_pte(&ptable[ptable_idx], pframe);   
      ptable_idx--;
   }
}

static void create_kernel_dir(PDE *pdir) {
   PTE *ptable;
   uint32_t pdir_idx, ptable_idx, pframe;

   for (pdir_idx = 0; pdir_idx < NR_PDE; pdir_idx ++) {
      make_invalid_pde(&pdir[pdir_idx]);
   }
        pframe = 0;
   for(pdir_idx = 0; pdir_idx < KERNEL_MEM / PD_SIZE; pdir_idx ++) {
      ptable = (PTE *)get_idle_page();

      memset(ptable, 0, PAGE_SIZE);
      make_pde(&pdir[pdir_idx], ptable);
      pdir[pdir_idx].user_supervisor = 0;          //user process shouldn't visit kernel space 
      make_pde(&pdir[pdir_idx + KOFFSET / PD_SIZE], ptable);
      for (ptable_idx = 0; ptable_idx < NR_PTE; ptable_idx ++) {
         make_pte(ptable, (void*)(pframe << 12));
         pframe ++;
         ptable ++;
      }
   }
}


static void load_segment(PCB *pcb, PDE* pdir, struct ProgramHeader* ph, int elf_fd){
   PTE *ptable;
   uint8_t *pframe;
   uint32_t pdir_idx, ptable_idx;

   pdir_idx = (uint32_t)(ph->vaddr >> 22);
   if(!pdir[pdir_idx].present) {
      ptable = (PTE *)get_idle_page();
      memset(ptable, 0, PAGE_SIZE);
      make_pde(&pdir[pdir_idx], ptable);
   }
   ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
   ptable_idx = (uint32_t)((ph->vaddr >> 12) & 0x3ff);
   if(!ptable[ptable_idx].present) {
      pframe = (uint8_t *)get_idle_page();
      memset(pframe, 0, PAGE_SIZE);
      make_pte(&ptable[ptable_idx], pframe);
   }
     
     /* ph->vaddr may be not the page's first addr;
        Only when we allocate memsz's memory , the page allocate is end.
     */
   int temp = PAGE_SIZE - (uint32_t)(ph->vaddr & 0xfff);
   int first_mem_len = ((temp < ph->memsz) ? temp : ph->memsz);

   int mem_rest_size = ph->memsz - first_mem_len;
   ptable_idx++;

   while(mem_rest_size > 0) {
      pframe = (uint8_t *)get_idle_page();
      memset(pframe, 0, PAGE_SIZE);
      if(ptable_idx == NR_PTE) {  // Need new ptable.
         pdir_idx++;
         ptable = (PTE*)get_idle_page();
         memset(ptable, 0, PAGE_SIZE);
         make_pde(&pdir[pdir_idx], ptable);
         ptable_idx = 0;
      }
      make_pte(&ptable[ptable_idx], pframe);

      ptable_idx++;
      mem_rest_size -= PAGE_SIZE;
   }
    
   read_elf(elf_fd, pcb->pid, (uint8_t*)ph->vaddr, ph->off, ph->filesz);   
}



static void handle_child(Msg *m) {
   PCB *ppcb = &pcb_pool[m->i[0]];
   PCB *cpcb = &pcb_pool[m->i[1]];
   create_kernel_dir(cpcb->kpdir);  //map kernel dir
   copy_process(ppcb, cpcb);
   m->dest = m->src;
   m->src = MEMORY;
   send(m->dest, m);
}

static void copy_process(PCB *ppcb, PCB *cpcb) {
   PDE *p_pdir = (PDE *)va_to_pa(ppcb->kpdir);
   PDE *c_pdir = (PDE *)va_to_pa(cpcb->kpdir);

   cpcb->cr3.val = 0;
   cpcb->cr3.page_directory_base = ((uint32_t)c_pdir) >> 12;

   PTE *p_ptable, *c_ptable;
   uint8_t *p_pframe, *c_pframe;
   uint32_t pdir_idx, ptable_idx;
     
   pdir_idx = KERNEL_MEM / PD_SIZE;
   uint32_t pdir_end = 0xc0000000 / PD_SIZE;
   for( ; pdir_idx < pdir_end; pdir_idx++) {     
      if(p_pdir[pdir_idx].present) {
         p_ptable = (PTE *)(p_pdir[pdir_idx].page_frame << 12);
         c_ptable = (PTE *)get_idle_page();
         memset(c_ptable, 0, PAGE_SIZE);
         make_pde(&c_pdir[pdir_idx], c_ptable);
         for(ptable_idx = 0; ptable_idx < NR_PTE; ptable_idx++) {
            if(p_ptable[ptable_idx].present) {
               p_pframe = (uint8_t *)(p_ptable[ptable_idx].page_frame << 12);
               c_pframe = (uint8_t *)get_idle_page();
               memcpy(c_pframe, p_pframe, PAGE_SIZE);
               make_pte(&c_ptable[ptable_idx], c_pframe);
            }
         }
      }
   }
}

static void takeback_page(PDE *pdir) {
   PTE *ptable;
   uint8_t *pframe;
   uint32_t pdir_idx, ptable_idx;
   uint32_t pdir_end = KERNEL_MEM / PD_SIZE;
   for(pdir_idx = 0; pdir_idx < pdir_end; pdir_idx++) {
      ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
      set_page_idle((uint32_t)ptable);
   }
   pdir_idx = KERNEL_MEM / PD_SIZE;
   pdir_end = 0xc0000000 / PD_SIZE;
   for( ; pdir_idx < pdir_end; pdir_idx++) {
        if(pdir[pdir_idx].present) {
           ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
           for(ptable_idx = 0; ptable_idx < NR_PTE; ptable_idx++) {
              if(ptable[ptable_idx].present) {
                 pframe = (uint8_t *) (ptable[ptable_idx].page_frame << 12);
                 set_page_idle((uint32_t)pframe);
                 make_invalid_pte(&ptable[ptable_idx]);
              }
           }
           set_page_idle((uint32_t)ptable);
           make_invalid_pde(&pdir[pdir_idx]);
        }
   }
}


static void handle_exec(Msg *m) {
   PCB *pcb = &pcb_pool[m->i[0]];
   char* file_name = (char*)m->i[1];

   int elf_fd = open_elf_file(file_name);
   if(elf_fd == -1) {
      m->dest = m->src;
      m->src = MEMORY;
      m->ret = -1;
      send(m->dest, m);
      return;
   }
   /*takeback old pages, get new pages and load program*/
   PDE *pdir = (PDE*)va_to_pa(pcb->kpdir);
   takeback_page(pdir);
   load_program(pcb, elf_fd);

   close_elf_file(elf_fd);

   m->dest = m->src;
   m->src = MEMORY;
   m->ret = 0;
   send(m->dest, m);
}


static void handle_exit(Msg *m) {
   PCB *pcb = &pcb_pool[m->i[0]];
   PDE *pdir = (PDE*)va_to_pa(pcb->kpdir);
   takeback_page(pdir);
   m->dest = m->src;
   m->src = MEMORY;
   send(m->dest, m);
}

int open_elf_file(char* file_name) {
    Msg m;
    m.src = MEMORY;
    m.i[0] = (int)file_name;
    m.i[1] = 0;
    m.dest = FILE;
    m.type = OPEN;
    send(FILE, &m);
    receive(FILE, &m);
    return m.ret;
}

void close_elf_file(int fd) {
    Msg m;
    m.src = MEMORY;
    m.dest = FILE;
    m.i[0] = fd;
    m.type = CLOSE;
    send(FILE, &m);
    receive(FILE, &m);
}

void read_elf(int elf_fd, pid_t req_pid, uint8_t* buf, int offset, int size) {
   Msg m;
   m.src = MEMORY;
   m.dest = FILE;
   m.i[0] = elf_fd;
   m.i[1] = offset;
   m.i[2] = 0;       //SEEK_SET
   m.type = LSEEK;
   send(FILE, &m);
   receive(FILE, &m);

   m.src = MEMORY;
   m.dest = FILE;
   m.i[0] = req_pid;
   m.i[1] = elf_fd;
   m.i[2] = (int)buf;
   m.i[3] = size;
   m.type = READ;
   send(FILE, &m);
   receive(FILE, &m);
}


/*static void load_segment(PDE* pdir, struct ProgramHeader* ph, int file_name){
   PTE *ptable;
   uint8_t *pframe;
   uint32_t pdir_idx, ptable_idx;
   uint32_t offset;

   pdir_idx = (uint32_t)(ph->vaddr >> 22);
   if(!pdir[pdir_idx].present) {
      ptable = (PTE *)get_idle_page();
      memset(ptable, 0, PAGE_SIZE);
      make_pde(&pdir[pdir_idx], ptable);
   }
   ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
   ptable_idx = (uint32_t)((ph->vaddr >> 12) & 0x3ff);
   if(!ptable[ptable_idx].present) {
      pframe = (uint8_t *)get_idle_page();
      memset(pframe, 0, PAGE_SIZE);
      make_pte(&ptable[ptable_idx], pframe);
   }
   pframe = (uint8_t *)(ptable[ptable_idx].page_frame << 12);
     
    ph->addr may be not the page's first addr;
        filesz may bigger or smaller than page_size_rest, or equal to it;
        memsz like filesz.
        Only when we allocate memsz's memory , the page allocate is end.
        But when filesz's data has been read, we shouldn't to read more from ramdisk.
    
    uint8_t* pa = pframe + (ph->vaddr & 0xfff);   //ph->vaddr may not page's first addr;
     int temp = PAGE_SIZE - (uint32_t)(ph->vaddr & 0xfff);
     int first_file_len = ((temp < ph->filesz) ? temp : ph->filesz);
     int first_mem_len = ((temp < ph->memsz) ? temp : ph->memsz);
     offset = ph->off;
     do_read(file_name, pa, offset, first_file_len);

     int mem_rest_size = ph->memsz - first_mem_len;
     int file_rest_size = ph->filesz - first_file_len;
     offset += first_file_len;
     ptable_idx++;

   while(mem_rest_size > 0) {
        pframe = (uint8_t *)get_idle_page();
        memset(pframe, 0, PAGE_SIZE);
      if(ptable_idx == NR_PTE) {  // Need new ptable.
         pdir_idx++;
         ptable = (PTE*)get_idle_page();
         memset(ptable, 0, PAGE_SIZE);
         make_pde(&pdir[pdir_idx], ptable);
         ptable_idx = 0;
      }
      make_pte(&ptable[ptable_idx], pframe);
      uint32_t read_len = (PAGE_SIZE < file_rest_size) ? PAGE_SIZE : file_rest_size;
      if(read_len){
          do_read(file_name, pframe, offset, read_len);    //load from ramdisk 
      }
      ptable_idx++;
      offset += read_len;
      file_rest_size -= read_len;
      mem_rest_size -= PAGE_SIZE;
   }
}*/


/*   uint8_t* start = ph->vaddr + ph->filesz;
   uint8_t *end  = ph->addr + ph->memsz;

   pdir_idx = (uint32_t)(ph->vaddr >> 22);
   ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
   ptable_idx = uint32_t((ph->vaddr >> 12) & 0x3ff);
   pframe = (uint8_t *)(ptable[ptable_idx].page_frame << 12);
   pframe_idx = (uint32_t)(start & 0xfff);
   while(start < end) {
      if(pframe_idx == PAGE_SIZE) {
         ptable_idx ++;
         if(ptable_idx == NR_PTE) {
            pdir_idx++;
            ptable = (PTE *)(pdir[pdir_idx].page_frame << 12);
            ptable_idx = 0;
         }
         pframe = (uint8_t *)(ptable[ptable_idx].page_frame << 12);
         pframe_idx = 0;
      }
      pa = pframe + pframe_idx;
      *pa = 0;
      pframe_idx++;
      start++;
   }*/


//simple memory management
/*static void create_process_space(PCB *pcb) {   //create space and load program.
   PDE *pdir = (PDE *)va_to_pa(pcb->kpdir);
   PTE *ptable = (PTE *)va_to_pa(pcb->kptable);
   uint32_t pdir_idx, ptable_idx, pframe_idx;
     
     for (pdir_idx = 0; pdir_idx < NR_PDE; pdir_idx ++) {
      make_invalid_pde(&pdir[pdir_idx]);
   }

   pframe_idx = 0;
   for (pdir_idx = 0; pdir_idx < KERNEL_MEM / PD_SIZE; pdir_idx ++) {      //kernel 16M
      make_pde(&pdir[pdir_idx], ptable);
      make_pde(&pdir[pdir_idx + KOFFSET / PD_SIZE], ptable);                
      for (ptable_idx = 0; ptable_idx < NR_PTE; ptable_idx ++) {
         make_pte(ptable, (void*)(pframe_idx << 12));
         pframe_idx ++;
         ptable ++;
      }
   }

     pframe_idx = (KERNEL_MEM + pcb->pid*USER_MEM) / 4096;          //user memory
   pdir_idx = (START_ADDR) / PD_SIZE;                          //virtual address 0x08048000
   uint32_t end = (START_ADDR - pdir_idx*PD_SIZE) / 4096;
   make_pde(&pdir[pdir_idx], ptable);
   for(ptable_idx = 0; ptable_idx < end; ptable_idx++) {     //72 entry seted invalid
      make_invalid_pte(ptable++);
   }

   for(ptable_idx = end; ptable_idx < NR_PTE; ptable_idx++) {
      make_pte(ptable, (void*)(pframe_idx << 12));
          pframe_idx ++;
          ptable ++;
   }

   pdir_idx ++;
   make_pde(&pdir[pdir_idx], ptable);
   for(ptable_idx = 0; ptable_idx < end; ptable_idx++) {   //72 entry need set
      make_pte(ptable, (void*)(pframe_idx << 12));
          pframe_idx ++;
          ptable ++;
   }
   for(ptable_idx = end; ptable_idx < NR_PTE; ptable_idx++) {
      make_invalid_pte(ptable++);
   }
   pcb->cr3.val = 0;
   pcb->cr3.page_directory_base = ((uint32_t)pdir) >> 12;
}*/

/*static void load_program(PCB *pcb, int file_name) {
       struct ELFHeader *elf;
   struct ProgramHeader *ph, *eph;
   unsigned char* pa, *i;
 
     elf = (struct ELFHeader*)0x8000;

     do_read(file_name, (uint8_t*)elf, 0, 4096);

     int offset_usr = START_ADDR - (KERNEL_MEM + pcb->pid* USER_MEM);

     ph = (struct ProgramHeader*)((unsigned char *)elf + elf->phoff);
   eph = ph + elf->phnum;
   for(; ph < eph; ph ++) {

      pa = (unsigned char*)(ph->paddr - offset_usr); //physical address 
      do_read(file_name, pa, ph->off, ph->filesz);    //load from ramdisk 
      for (i = pa + ph->filesz; i < pa + ph->memsz; *i ++ = 0);
   }
     pcb->entry = (void*)elf->entry;
}*/


