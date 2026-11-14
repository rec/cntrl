/* Link-only boundary fixture. This image is never executed. */
.section .text.itcm,"ax",%progbits
.space memory_fixture_bytes
.section .budget_exidx,"a",%progbits
.space 8
