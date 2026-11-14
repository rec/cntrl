/* One allocation budget, derived from this firmware's final static RAM2 use. */
cntrl_memory_budget = _heap_end - _heap_start;
ASSERT(cntrl_memory_budget >= 116,
       "No RAM2 heap remains for setups and fonts")

/* Preserve RAM1 for data/stack and leave growth room in the six ITCM banks.
 * Count the same sections as the SDK's _itcm_block_count expression. */
cntrl_itcm_capacity = 6 * 32768;
cntrl_itcm_used = SIZEOF(.text.itcm) + SIZEOF(.ARM.exidx);
cntrl_itcm_headroom = cntrl_itcm_capacity - cntrl_itcm_used;
ASSERT(cntrl_itcm_used + 4096 <= cntrl_itcm_capacity,
       "ITCM budget exceeded: require 4096 bytes spare within six 32 KiB banks")
