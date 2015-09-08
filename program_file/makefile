CC      = gcc
LD      = ld
CFLAGS  = -m32 -static -ggdb -MD -Wall -Werror -I./include -O2 \
		 -fno-builtin -fno-stack-protector -fno-omit-frame-pointer -Wno-main
ASFLAGS = -m32 -MD -I./include
LDFLAGS = -melf_i386
QEMU    = qemu-system-i386

CFILES  = $(shell find -name "*.c")
SFILES  = $(shell find -name "*.S")
OBJS    = first_program.o ../src/kernel/system_call/syscall.o

first_program.h : first_program
	xxd -g 1 -i first_program > ../src/kernel/process/first_program.h

first_program: $(OBJS)
	$(LD) $(LDFLAGS) -e main -o first_program $(OBJS)
	objdump -D first_program > code.txt	# disassemble result
	readelf -a first_program > elf.txt		# obtain more information about the executable

-include $(OBJS:.o=.d)

clean:
	rm -f first_program $(OBJS) $(OBJS:.o=.d)
