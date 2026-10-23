#pragma once
#include <SD.h>
#include <cstdio>

// Diagnostic only: synchronous, read-only traversal, streamed without retaining
// a whole catalogue in RAM. Names are hex encoded so spaces/newlines are unambiguous.
inline bool list_storage(FsFile &directory, unsigned parent, unsigned &index) {
    FsFile entry;
    while (entry.openNext(&directory, O_RDONLY)) {
        char name[256], line[600];
        if (!entry.getName(name, sizeof(name))) return false;
        unsigned id = ++index;
        uint64_t size = entry.size();
        // The target's compact printf does not support %llu. Two 32-bit hex
        // halves preserve exFAT sizes above 4 GiB without changing the C library.
        int used = std::snprintf(line, sizeof(line), "CNTRL ENTRY %u %u %c %08lx%08lx ",
            id, parent, entry.isDir() ? 'D' : 'F',
            (unsigned long)(size >> 32), (unsigned long)(size & 0xffffffff));
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
            used += std::snprintf(line + used, sizeof(line) - used, "%02x", *p);
        line[used++] = '\n';
        if (Serial.write(reinterpret_cast<const uint8_t *>(line), used) != unsigned(used)) return false;
        if (entry.isDir() && !list_storage(entry, id, index)) return false;
        entry.close();
    }
    return !directory.getError();
}
