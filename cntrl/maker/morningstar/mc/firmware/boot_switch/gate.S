.syntax unified
.cpu cortex-m7
.thumb
.section .boot_gate,"ax",%progbits
.global boot_gate
.thumb_func
boot_gate:
    push {r4, lr}
    bl boot_selected
    cmp r0, #0
    pop {r4, lr}
    beq stock
    ldr r0, =custom_entry
    bx r0
stock:
    bx lr
