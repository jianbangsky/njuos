#include "common.h"



extern void serial_printc(char);

void printf_int(int val);
void printf_int_x(unsigned int val);

void printk(const char* format, ...) {
   void* cur_address = (char*)&format + sizeof(char**);
   int i = 0;
   while(format[i] != '\0') {
     if(format[i] == '%') {
       i++;
       switch(format[i]) {
          case 'd' : 
          {
             int  val = *((int*) cur_address);
             cur_address = (char*)cur_address + sizeof(int);
             printf_int(val);
             break;
          } 
          case 'x' :
          {
             unsigned int val = *((unsigned int*) cur_address);
             cur_address = (char*)cur_address + sizeof(int);
             printf_int_x(val);
             break;
          }
          case 'c' :
          {
             char val = *((char*) cur_address);
             cur_address = (char*)cur_address + sizeof(int);
             serial_printc(val);
             break;
          }
          case 's' :
          {
             char* str = (char*)(*((char**)cur_address));
             while(*str != '\0') {
                serial_printc(*str);
                str++;
             }
             cur_address = (char*)cur_address + sizeof(char**);
             break;
          }
          case '%' :
             serial_printc('%');
             break;
       }
    }
    else {
       serial_printc(format[i]);
    }

    i++;            

  }
}

void printf_int(int val) {
   int flag = 0;
   if(val == 0x80000000) {
      val++;
      flag = 1;
   }
   if(val < 0) {
      serial_printc('-');
      val = 0 - val;
   }
   char num[15];
   int top = -1;
   if(val == 0) {
      serial_printc('0');
      return;
   }
   while(val != 0) {
      num[++top] = '0' + (val % 10);
      val /= 10;
   }
   for(; top >= 1; top--) 
      serial_printc(num[top]);
   serial_printc(num[0] + flag);
}

void printf_int_x(unsigned val) {
   char num[15];
   int top=-1;
   if(val == 0) {
      serial_printc('0');
      return;
   }
   while(val != 0) {
      if(val % 16 < 10) {
          num[++top] = '0' + (val % 16);
      }
      else {
          num[++top] = 'a' + (val % 16 -10);
      }
      val /= 16;
   }
   for(; top >= 0; top--) 
      serial_printc(num[top]);
}