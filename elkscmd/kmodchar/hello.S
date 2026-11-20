/* Separately loaded, position independent i8086 module for KMC1. */
        .arch i8086
        .code16
        .section .text
        .ascii "KMC1"
        .word text_end-text_start, read_byte-text_start
        .word message_end-message
text_start:
message:
        .ascii "ELKS module OK\n"
message_end:
read_byte:
        pushw %bp
        movw %sp,%bp
        pushw %bx
        movw 6(%bp),%bx
        xorw %ax,%ax
        movb %cs:message-text_start(%bx),%al
        popw %bx
        popw %bp
        lret
text_end:
