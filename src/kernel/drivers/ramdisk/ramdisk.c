#include "kernel.h"
#include "x86/x86.h"
#include "hal.h"
#include "string.h"

/*#define NR_MAX_FILE 8
#define NR_FILE_SIZE (128 * 1024)*/
#define RAMDISK_SIZE (4*1024*1024)
#define ANY -1

pid_t RAMDISK;
 
//static uint8_t file[NR_MAX_FILE][NR_FILE_SIZE];
static uint8_t disk[RAMDISK_SIZE];
static void read_ram(Msg *m);
void write_ram(Msg *m);
static void ram_driver_thread(void);
//static void test_init();


void init_ramdisk(void) {
    PCB *p = create_kthread(ram_driver_thread);
    RAMDISK = p -> pid;
    wakeup(p);
    hal_register("ram", RAMDISK, 0);
    //test_init();
}

void ram_driver_thread(void) {
    Msg m;
    while(true) {
        receive(ANY, &m);
        switch(m.type) {
            case DEV_READ: read_ram(&m);
                break;
            case DEV_WRITE: write_ram(&m);
                break;
            default: assert(0);
        }
    }
}

void read_ram(Msg *m) {
    assert(m->offset >= 0);
    assert(m->offset + m->len < RAMDISK_SIZE);

    copy_from_kernel(fetch_pcb(m->req_pid), m->buf, disk+m->offset, m->len);

    m->dest = m->src;
    m->src = RAMDISK;
    m->ret = m->len;
    send(m->dest, m);    //ack to filemanage
}

void write_ram(Msg *m) {
    //printk("m->off%d\n", m->offset);
    //printk("m->len%d\n", m->len);
    assert(m->offset >= 0);
    assert(m->offset + m->len < RAMDISK_SIZE);
    copy_to_kernel(fetch_pcb(m->req_pid), disk+m->offset, m->buf, m->len);

    m->dest = m->src;
    m->src = RAMDISK;
    m->ret = m->len;
    send(m->dest, m);
}

/*static void test_init() {
        memcpy(disk, shell, shell_len);
        void *next_file = disk + NR_FILE_SIZE;
        memcpy(next_file, first_program, first_program_len);
}*/