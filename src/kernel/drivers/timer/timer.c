#include "kernel.h"
#include "x86/x86.h"
#include "hal.h"
#include "time.h"
#include "string.h"

#define PORT_TIME 0x40
#define PORT_RTC  0x70
#define FREQ_8253 1193182
#define NEW_TIMER -1

typedef struct Time_node{
	int pid;
	int sec;
	ListHead list;
} Time_node;

pid_t TIMER;
static long jiffy = 0;
static Time rt;

static ListHead wait_queue;     //timing event wait queue;
static ListHead free_list;
static Time_node time_vec[100];

static void update_jiffy(void);
static void init_i8253(void);
static void init_rt(void);
static void timer_driver_thread(void);
static void init_wait_list();
static void add_time_event(Msg *m);

void init_timer(void) {
	init_i8253();
	init_rt();
	init_wait_list();       //timing event
	add_irq_handle(0, update_jiffy);
	PCB *p = create_kthread(timer_driver_thread);
	TIMER = p->pid;
	wakeup(p);
	hal_register("timer", TIMER, 0);
}

static void
timer_driver_thread(void) {
	static Msg m;
	while (true) {
		receive(ANY, &m);
		switch (m.type) {
                                case NEW_TIMER: add_time_event(&m);
                                        break;
		      default: assert(0);
		}
	}
}

static void add_time_event(Msg *m) {
    Time_node *new_time = list_entry(free_list.next, Time_node, list);
    list_del(&new_time->list);
    new_time->pid = m->src;
    new_time->sec = m->i[0]; 
    ListHead *ptr;
    if(new_time->sec <= 0) {
        m->src = TIMER;
        send(new_time->pid, m);
        return;
    }
    list_foreach(ptr, &wait_queue) {
        Time_node *node = list_entry(ptr, Time_node, list);
        if(new_time->sec < node->sec) {
        	    node->sec -= new_time->sec;
        	    break;
        } else {
        	    new_time->sec -= node->sec;
        }
    }
    list_add_before(ptr, &new_time->list);
}

static void init_wait_list() {
	list_init(&wait_queue);
	list_init(&free_list);
	int i;
	for(i = 0; i < 100; i++) {
	    list_add_before(&free_list, &time_vec[i].list);
	}
}

static void time_event_proc() {
    if(!list_empty(&wait_queue)) {
    	    Time_node *first = list_entry(wait_queue.next, Time_node, list);
    	    first -> sec--;
    	    ListHead *ptr = wait_queue.next;
    	    while(ptr != &wait_queue) {
    	    	    Time_node *node = list_entry(ptr, Time_node, list);
    	    	    if(node->sec == 0) {
    	    	    	    Msg m;
    	    	    	    m.src = TIMER;
    	    	    	    send(node->pid, &m);
    	    	    	    ListHead *p = ptr;
    	    	    	    ptr = ptr -> next;
    	    	    	    list_del(p);
    	    	    	    list_add_before(&free_list, p);
    	    	    } else {
    	    	    	    break;
    	    	    }
    	    }
    }
}



long
get_jiffy() {
	return jiffy;
}

static int
md(int year, int month) {
	bool leap = (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
	static int tab[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	return tab[month] + (leap && month == 2);
}

static void
update_jiffy(void) {
	jiffy ++;
	if (jiffy % HZ == 0) {
		rt.second ++;
		if (rt.second >= 60) { rt.second = 0; rt.minute ++; }
		if (rt.minute >= 60) { rt.minute = 0; rt.hour ++; }
		if (rt.hour >= 24)   { rt.hour = 0;   rt.day ++;}
		if (rt.day >= md(rt.year, rt.month)) { rt.day = 1; rt.month ++; } 
		if (rt.month >= 13)  { rt.month = 1;  rt.year ++; }

                           time_event_proc();
 
	}
}

static void
init_i8253(void) {
	int count = FREQ_8253 / HZ;
	assert(count < 65536);
	out_byte(PORT_TIME + 3, 0x34);
	out_byte(PORT_TIME, count & 0xff);
	out_byte(PORT_TIME, count >> 8);	
}


static int get_one_data(unsigned char reg_no) {
    int num;
    unsigned char num_bcd = 0;
    asm volatile("out %0,%1" : : "a" (reg_no), "d" ((unsigned short)0x70));
    asm volatile("in %1,%0" : "=a" (num_bcd) : "d" ((unsigned short)0x71)); 
    num = (num_bcd/16) * 10 + (num_bcd%16);
    return num;
}

uint8_t regis_no[] = {0x00, 0x02, 0x04, 0x06, 0x07, 0x08, 0x09, 0x32, 0x0A, 0x0B};

static void
init_rt(void) {
    memset(&rt, 0, sizeof(Time));
    /* Optional: Insert code here to initialize current time correctly */
     rt.second = get_one_data(regis_no[0]);
     rt.minute = get_one_data(regis_no[1]);
     rt.hour = get_one_data(regis_no[2]);
     rt.day = get_one_data(regis_no[4]);
     rt.month = get_one_data(regis_no[5]);
     rt.year = get_one_data(regis_no[6]);
}

void 
get_time(Time *tm) {
	memcpy(tm, &rt, sizeof(Time));
}
