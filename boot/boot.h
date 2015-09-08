/* Structure of a ELF binary header */
struct ELFHeader {
   unsigned int   magic;
   unsigned char  elf[12];
   unsigned short type;
   unsigned short machine;
   unsigned int   version;
   unsigned int   entry;
   unsigned int   phoff;
   unsigned int   shoff;
   unsigned int   flags;
   unsigned short ehsize;
   unsigned short phentsize;
   unsigned short phnum;
   unsigned short shentsize;
   unsigned short shnum;
   unsigned short shstrndx;
};

/* Structure of program header inside ELF binary */
struct ProgramHeader {
   unsigned int type;
   unsigned int off;
   unsigned int vaddr;
   unsigned int paddr;
   unsigned int filesz;
   unsigned int memsz;
   unsigned int flags;
   unsigned int align;
};

/* The I/O wrapper functions.
   See "GCC-Inline-Assembly-HOWTO" for more detials.
   the keyword "volatile" cannot be ommited otherwise the compiler
   optimizier will move the assembly out of a loop. */
static inline char
in_byte(short port) {
   char data;
   asm volatile("in %1,%0" : "=a" (data) : "d" (port));
   return data;
}
static inline int 
in_long(short port) {
   int data;
   asm volatile("in %1, %0" : "=a" (data) : "d" (port));
   return data;
}
static inline void
out_byte(short port, char data) {
   asm volatile("out %0,%1" : : "a" (data), "d" (port));
}


