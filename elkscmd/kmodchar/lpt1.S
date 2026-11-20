/* KMC2: i8086 bounded Centronics byte output on LPT1 at 0x378. */
#include <kmodchar.h>

        .arch i8086
        .code16
        .section .text
        .ascii "KMC2"
        .word text_end-text_start, write_byte-text_start
        .word KMOD_LPT_PORT
text_start:
write_byte:
        pushw %bp
        movw %sp,%bp
        movw $(KMOD_LPT_PORT + 1),%dx
        movw $1000,%cx
poll:
        inb %dx,%al
        cmpb $0xff,%al
        je ioerr
        testb $0x20,%al             /* out of paper */
        jnz paper
        movb %al,%ah
        andb $0x18,%ah              /* selected and no error */
        cmpb $0x18,%ah
        jne ioerr
        testb $0x80,%al             /* active-low BUSY */
        jnz send
        loop poll
        movw $KMOD_LPT_BUSY,%ax
        jmp done
paper:
        movw $KMOD_LPT_PAPER,%ax
        jmp done
ioerr:
        movw $KMOD_LPT_IOERR,%ax
        jmp done
send:
        decw %dx
        movb 6(%bp),%al
        outb %al,%dx
        addw $2,%dx
        movb $0x0d,%al              /* select + no reset + strobe, IRQ off */
        outb %al,%dx
        decw %dx
        inb %dx,%al                 /* one I/O cycle before releasing strobe */
        incw %dx
        movb $0x0c,%al              /* keep selected, release strobe */
        outb %al,%dx
        xorw %ax,%ax
done:
        popw %bp
        lret
text_end:
