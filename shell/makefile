CC      = gcc
LD      = ld
CFLAGS  = -m32 -static -ggdb -MD -Wall -Werror -I./include -O2 \
		 -fno-builtin -fno-stack-protector -fno-omit-frame-pointer
ASFLAGS = -m32 -MD -I./include
LDFLAGS = -melf_i386
QEMU    = qemu-system-i386

CFILES  = $(shell find -name "*.c")
SFILES  = $(shell find -name "*.S")
OBJS    = shell.o ../src/kernel/system_call/syscall.o

shell.h : shell
	xxd -g 1 -i shell > ../src/kernel/process/shell.h

shell: $(OBJS)
	$(LD) $(LDFLAGS) -e main -o shell $(OBJS)
	objdump -D shell > code.txt	# disassemble result
	readelf -a shell > elf.txt		# obtain more information about the executable

-include $(OBJS:.o=.d)

clean:
	rm -f shell $(OBJS) $(OBJS:.o=.d)
