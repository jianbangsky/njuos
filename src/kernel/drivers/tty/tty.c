#include "kernel.h"
#include "hal.h"
#include "tty.h"

pid_t TTY;

void send_keymsg(void);

void init_console(void);
void init_getty(void);
static void ttyd(void);

void init_tty(void) {
   add_irq_handle(1, send_keymsg);
   PCB *p = create_kthread(ttyd);
   TTY = p->pid;
   wakeup(p);
   init_console();
   init_getty();
}

static void
ttyd(void) {
   Msg m;
   while (1) {
      receive(ANY, &m);
      if (m.src == MSG_HARD_INTR) {
         switch (m.type) {
            case MSG_TTY_GETKEY:
               readkey();
               break;
            case MSG_TTY_UPDATE:
               update_banner();
               break;
            default: assert(0);
         }
      } else {
         switch(m.type) {
            case DEV_READ:
               read_request(&m);
               break;
            case DEV_WRITE:
               if (m.dev_id >= 0 && m.dev_id < NR_TTY) {
                  char c;
                  int i;
                  for (i = 0; i < m.len; i ++) {
                     copy_to_kernel(fetch_pcb(m.req_pid), &c, (char*)m.buf + i, 1);
                     consl_writec(&ttys[m.dev_id], c);
                  }
                  consl_sync(&ttys[m.dev_id]);
               }
               else {
                  assert(0);
               }
               m.ret = m.len;
               pid_t dest = m.src;
               m.src = current->pid;
               send(dest, &m);
               break;
            default: assert(0);

         }
      }
   }
}

