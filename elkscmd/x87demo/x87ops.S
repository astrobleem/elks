/*
 * 16-bit real-mode IBM-PC x87 demo. Everything ahead of SMSW is safe on
 * 8086; the FLAGS test requires the 386 real-mode high-FLAGS behavior.
 * BIOS equipment word is a conservative presence hint, not an FPU probe.
 */
        .arch i8086
        .code16
        .section .text
        .globl x87_ready
        .type x87_ready,@function
x87_ready:
        pushf
        popw %cx
        movw %cx,%ax
        andw $0x0fff,%ax
        pushw %ax
        popf
        pushf
        popw %dx
        pushw %cx
        popf
        testw $0xf000,%dx         /* fixed ones on the 8086 family */
        jnz .Lunsupported
        movw %cx,%ax
        orw $0x7000,%ax
        pushw %ax
        popf
        pushf
        popw %dx
        pushw %cx
        popf
        testw $0x7000,%dx         /* fixed zero on real-mode 286 */
        jz .Lunsupported

        .arch i386
        .arch .387
        smsw %ax
        testw $1,%ax              /* PE: don't use real-mode BDA or CLI */
        jnz .Lunsupported
        testw $0x000c,%ax         /* EM or TS: refuse, never change CR0 */
        jnz .Ldisabled
        pushw %es
        movw $0x40,%dx
        movw %dx,%es
        testb $0x02,%es:0x10      /* BIOS equipment word, coprocessor bit */
        popw %es
        jz .Lno_fpu
        xorw %ax,%ax
        ret
.Lunsupported:
        movw $1,%ax
        ret
.Lno_fpu:
        movw $2,%ax
        ret
.Ldisabled:
        movw $3,%ax
        ret
        .size x87_ready,.-x87_ready

        .globl x87_measure
        .type x87_measure,@function
x87_measure:
        pushw %bp
        movw %sp,%bp
        pushw %bx
        subw $94,%sp             /* 16-bit FNSAVE image, below saved BX */
        movw 4(%bp),%bx
        pushf
        cli                      /* no ELKS schedule/IRQ in this bounded region */
        fnsave -96(%bp)           /* save incoming state and initialize x87 */
        fwait

        filds %cs:.L144
        fsqrt
        fistpl 4(%bx)

        filds (%bx)
        fimuls (%bx)
        filds 2(%bx)
        fimuls 2(%bx)
        faddp %st,%st(1)
        fsqrt
        fimuls %cs:.L1000
        fistpl 8(%bx)
        fwait

        frstor -96(%bp)
        popf                     /* restore the original IRQ enable state */
        addw $94,%sp
        popw %bx
        popw %bp
        ret
        .size x87_measure,.-x87_measure
.L144:
        .word 144
.L1000:
        .word 1000
