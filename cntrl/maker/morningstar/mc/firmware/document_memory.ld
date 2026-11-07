/* The document pool is dynamic, but its configured maximum must fit RAM2. */
ASSERT(_heap_end - _heap_start >= cntrl_document_memory_budget,
       "Document memory budget exceeds available RAM2 heap")
