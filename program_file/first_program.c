#include "../include/sys_call.h"

int a[100000] = {1, 2, 3, 4};
int b[100000];

size_t strlen(const char *str) {
   int len = 0;
   while (*str ++) len ++;
   return len;
}

int main(char *args) {
   //sys_sleep(2);
   puts(args);
   puts("\n");
   int fd = open("new_file", 1, 0);
   if(fd == -1) {
      exit(1);
   }
   char *s = "This is new_file we created! You can read and write it as you like!\nthanks";
   write(fd, (uint8_t*)s, strlen(s) + 1);
   char buf[256];
   lseek(fd, 0, 0);
   read(fd, (uint8_t*)buf, 256);
   puts(buf);
   puts("\n");
   exit(0);
   return 0;
}


/*volatile int x = 0;
int y[1000000];
int k = 0;
int main() {
   while(1) {
      if(x % 10000000 == 0) {
         puts("I'm first program which is putting words on screen");
         //asm volatile("movl $105, %eax; int $0x80");
      }
          y[k] = x;
      x ++;
      k = (k+1)%1000000;
   }
   return 0;
}*/

