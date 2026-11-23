.syntax unified
.cpu cortex-m7
.thumb
.section .boot_gate,"ax",%progbits
.global boot_gate
.thumb_func
boot_gate:
    push {r4, lr}
    bl boot_entry
    cmp r0, #0
    pop {r4, lr}
    beq stock
    bx r0
stock:
    bx lr
