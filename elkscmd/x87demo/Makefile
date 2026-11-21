# Build explicitly with the normal ia16 ELKS toolchain; not a default app.
CC = ia16-elf-gcc
CFLAGS = -Os -Wall -Wextra -march=i8086 -melks-libc -mregparmcall

.PHONY: all clean
all: x87demo

x87demo: x87demo.c x87ops.S
	$(CC) $(CFLAGS) -o $@ x87demo.c x87ops.S

clean:
	$(RM) x87demo
