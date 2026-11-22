/* PC-98 console beeper: retain BIOS pitch, switch only the buzzer inhibit. */
#include <kmodchar.h>
        .arch i8086
        .code16
        .section .text
        .ascii "KMCB"
        .word text_end-text_start, 0, KMOD_PC98_BEEP_PORT
text_start:
        pushw %bp
        movw %sp,%bp
        movb $7,%al             /* set 8255 port C bit 3: inhibit */
        cmpw $KMOD_PC98_BEEP_ON,6(%bp)
        jne 1f
        decb %al               /* reset bit 3: enable buzzer */
1:
        outb %al,$KMOD_PC98_BEEP_PORT
        xorw %ax,%ax
        popw %bp
        lret
text_end:
