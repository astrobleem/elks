# Optional demo, intentionally not part of default ELKS userspace builds.
CC = ia16-elf-gcc
OBJCOPY = ia16-elf-objcopy
CFLAGS = -I../../include -Os -Wall -Wextra -march=i8086 -melks-libc -mregparmcall

.PHONY: all clean
all: hello.kmc kmodctl kmodtest

hello.o: hello.S
	$(CC) -march=i8086 -c -o $@ $<

hello.kmc: hello.o
	$(OBJCOPY) -O binary -j .text $< $@

kmodctl: kmodctl.c ../../include/kmodchar.h
	$(CC) $(CFLAGS) -o $@ $<

kmodtest: kmodtest.c ../../include/kmodchar.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	$(RM) hello.o hello.kmc kmodctl kmodtest
