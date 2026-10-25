ENTRY(boot_gate)
SECTIONS
{
    .text 0x60083000 : {
        KEEP(*(.boot_gate))
        *(.text*)
        *(.rodata*)
    }
    .ARM.exidx : { *(.ARM.exidx*) }
    .selection 0x60084000 (NOLOAD) : { . += 0x1000; }
    .bss 0x20013ca0 (NOLOAD) : {
        *(.bss*)
        *(COMMON)
    }
    ASSERT(SIZEOF(.bss) <= 256, "Stock hook exceeds its reserved BSS")
    ASSERT(SIZEOF(.text) + SIZEOF(.ARM.exidx) <= 0x1000, "Stock hook exceeds its flash page")
    /DISCARD/ : { *(.comment) *(.ARM.attributes) }
}
