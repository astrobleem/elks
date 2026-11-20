/* KMC3: bounded DSP reset and IRQ acknowledgement for SB at 0x220. */
#include <kmodchar.h>

        .arch i8086
        .code16
        .section .text
        .ascii "KMC3"
        .word text_end-text_start, entry-text_start
        .word KMOD_SB_PORT
text_start:
entry:
        pushw %bp
        movw %sp,%bp
        movw 6(%bp),%ax
        cmpw $KMOD_SB_ACK,%ax
        je ack
        cmpw $KMOD_SB_RESET,%ax
        jne fail
        movw $(KMOD_SB_PORT + 6),%dx
        movb $1,%al
        outb %al,%dx
        outb %al,$0x80
        movw $200,%cx
settle:
        inb $0x61,%al
        outb %al,$0x80
        loop settle
        xorw %ax,%ax
        outb %al,%dx
        outb %al,$0x80
        movw $500,%cx
reset_wait:
        movw $(KMOD_SB_PORT + 0x0e),%dx
        inb %dx,%al
        outb %al,$0x80
        testb $0x80,%al
        jz reset_next
        subw $4,%dx
        inb %dx,%al
        outb %al,$0x80
        cmpb $0xaa,%al
        je ok
reset_next:
        loop reset_wait
fail:
        movw $1,%ax
        jmp done
ack:
        /* Read status to acknowledge 8-bit DSP IRQ; bound any drain. */
        movw $8,%cx
ack_next:
        movw $(KMOD_SB_PORT + 0x0e),%dx
        inb %dx,%al
        outb %al,$0x80
        testb $0x80,%al
        jz ok
        subw $4,%dx
        inb %dx,%al
        outb %al,$0x80
        loop ack_next
ok:
        xorw %ax,%ax
done:
        popw %bp
        lret
text_end:
