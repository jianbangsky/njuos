#include "kernel.h"
#include "tty.h"

static int tty_idx = 2;

static void
getty(void) {
   char name[] = "tty0", buf[256];
   lock();
   name[3] += (tty_idx ++);
   unlock();

   while(1) {
      /* Insert code here to do these:
       * 1. read key input from ttyd to buf (use dev_read())
       * 2. convert all small letters in buf into capitcal letters
       * 3. write the result on screen (use dev_write())
       */

      int nread = dev_read(name, current->pid, buf, 0, 256);
      if(!nread) {
         continue;
      }
             int i;
      for(i = 0; i < nread; i++) {
         if(buf[i] >= 'a' && buf[i] <= 'z') {
            buf[i] -= 'a' - 'A';
         }
      }
      int nwrite = dev_write(name, current->pid, buf, 0, nread);
      assert(nread == nwrite);
   }
}

void
init_getty(void) {
   int i;
   for(i = 0; i < NR_TTY-1; i ++) {
      wakeup(create_kthread(getty));
   }
}


