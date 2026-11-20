# Optional demo, intentionally not part of default ELKS userspace builds.
CC = ia16-elf-gcc
OBJCOPY = ia16-elf-objcopy
CFLAGS = -I../../include -Os -Wall -Wextra -march=i8086 -melks-libc -mregparmcall

.PHONY: all clean
all: hello.kmc lpt1.kmc kmodctl kmodtest lpttest

%.o: %.S ../../include/kmodchar.h
	$(CC) -I../../include -march=i8086 -c -o $@ $<

%.kmc: %.o
	$(OBJCOPY) -O binary -j .text $< $@

kmodctl: kmodctl.c ../../include/kmodchar.h
	$(CC) $(CFLAGS) -o $@ $<

kmodtest: kmodtest.c ../../include/kmodchar.h
	$(CC) $(CFLAGS) -o $@ $<

lpttest: lpttest.c ../../include/kmodchar.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	$(RM) hello.o hello.kmc lpt1.o lpt1.kmc kmodctl kmodtest lpttest
