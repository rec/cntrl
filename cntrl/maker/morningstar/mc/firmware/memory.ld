/* One allocation budget, derived from this firmware's final static RAM2 use. */
cntrl_memory_budget = _heap_end - _heap_start;
ASSERT(cntrl_memory_budget >= 116,
       "No RAM2 heap remains for setups and fonts")
