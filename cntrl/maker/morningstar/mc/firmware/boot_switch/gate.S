.syntax unified
.cpu cortex-m7
.thumb
.section .boot_gate,"ax",%progbits
.global boot_gate
.thumb_func
boot_gate:
    ldr r0, =0x400f8030
    ldr r1, [r0]
    ldr r2, =0x4354524c
    cmp r1, r2
    bne stock
    movs r1, #0
    str r1, [r0]
    dsb
    ldr r0, =custom_entry
    bx r0
stock:
    bx lr
