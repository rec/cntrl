#pragma once
#include <cntrl/startup_counter.h>

class StartupCounter : public cntrl::StartupCounter {
public:
    template<class File>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(startup_counter_read)
#endif
    static void read_command(const char *line, File &file, char (&response)[280]) {
        if (!file) { std::strcpy(response, "CNTRL ERROR starts-open"); return; }
        if (!std::strcmp(line, "CNTRL STARTS SIZE")) {
            char number[21]; *std::to_chars(number, number + 20, uint64_t(file.size())).ptr = 0;
            std::snprintf(response, sizeof(response), "CNTRL STARTS SIZE %s", number); return;
        }
        unsigned offset = 0; int end = 0;
        if (std::sscanf(line, "CNTRL STARTS READ %u%n", &offset, &end) != 1 || !end || line[end]) {
            std::strcpy(response, "CNTRL ERROR starts-command"); return;
        }
        if (offset >= file.size()) { std::strcpy(response, "CNTRL ERROR starts-offset"); return; }
        if (!file.seekSet(offset)) { std::strcpy(response, "CNTRL ERROR starts-seek"); return; }
        uint64_t remaining = file.size() - offset;
        unsigned n = remaining < 128 ? unsigned(remaining) : 128;
        char bytes[128];
        int read = file.read(bytes, n);
        if (read != int(n)) {
            std::snprintf(response, sizeof(response), "CNTRL ERROR starts-read %d/%u", read, n); return;
        }
        std::strcpy(response, "CNTRL DATA ");
        for (unsigned i = 0; i < n; ++i)
            std::snprintf(response + 11 + i * 2, 3, "%02x", uint8_t(bytes[i]));
    }

};
