#include "kernel.h"
#include "hal.h"
#include "ide.h"
#include "time.h"

#define WRITEBACK_TIME  1  /* writeback cache for every 1 second */

pid_t IDE;

static void ide_intr(void);
static void time_intr(void);
static void ide_driver_thread(void);

void cache_init(void);
void cache_writeback(void);
uint8_t read_byte(uint32_t);
void write_byte(uint32_t, uint8_t);

void
init_ide(void) {
	cache_init();
	add_irq_handle(14, ide_intr);
	add_irq_handle(0 , time_intr);
	PCB *p = create_kthread(ide_driver_thread);
	IDE = p->pid;
	hal_register("hda", IDE, 0);
	wakeup(p);
}

static void
ide_driver_thread(void) {
	static Msg m;


	while (true) {
		receive(ANY, &m);

		if (m.src == MSG_HARD_INTR) {
			if (m.type == IDE_WRITEBACK) {
				cache_writeback();
			} else {
				panic("IDE interrupt is leaking");
			}
		} else if (m.type == DEV_READ) {
			uint32_t i;
			uint8_t data;
			for (i = 0; i < m.len; i ++) {
				data = read_byte(m.offset + i);
				copy_from_kernel(fetch_pcb(m.req_pid), m.buf + i, &data, 1);
			}
			m.ret = i;
			m.dest = m.src;
			m.src = IDE;
			send(m.dest, &m);
		} else if (m.type == DEV_WRITE) {
			uint32_t i;
			uint8_t data;
			for (i = 0; i < m.len; i ++) {
				copy_to_kernel(fetch_pcb(m.req_pid), &data, m.buf + i, 1);
				write_byte(m.offset + i, data);
			}
			m.ret = i;
			m.dest = m.src;
			m.src = IDE;
			send(m.dest, &m);
		}
		else {
			assert(0);
		}
	}
}

static void
ide_intr(void) {
	static Msg m;
	m.type = IDE_READY;
	m.src = MSG_HARD_INTR;
	send(IDE, &m);
}
static void
time_intr(void) {
	static Msg m;
	static uint32_t counter = 0;
	counter = (counter + 1) % (WRITEBACK_TIME * HZ);
	if (counter == 0) {
		m.type = IDE_WRITEBACK;
		m.src = MSG_HARD_INTR;
		send(IDE, &m);
	}
}
