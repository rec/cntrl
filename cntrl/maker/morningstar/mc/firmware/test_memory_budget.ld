/* Give the production assertions their section sizes and heap symbols. */
SECTIONS {
    .text.itcm 0 : { *(.text.itcm) }
    .ARM.exidx : { *(.budget_exidx) }
    _heap_start = 0;
    _heap_end = 524288;
}
