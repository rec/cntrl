#pragma once
#include "filesystem.h"
#include "memory.h"
#include "usb_config.h"
#include <cstdio>
#include <cstring>

namespace cntrl {
// Read-only recovery access does not require understanding a stored format.
class StateTransfer {
public:
    explicit StateTransfer(files::FileSystem &filesystem) : filesystem_(filesystem) {}
    template<class Replies>
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(state_command)
#endif
    bool command(const char *line, Replies &replies) {
        if (std::strncmp(line, "CNTRL STATE ", 12)) return false;
        char hex[447] = {}, path[224] = {}; unsigned offset = 0; int end = 0;
        bool size = std::sscanf(line, "CNTRL STATE SIZE %446s%n", hex, &end) == 1 && end && !line[end];
        if (!size) {
            end = 0;
            if (std::sscanf(line, "CNTRL STATE READ %446s %u%n", hex, &offset, &end) != 2 || !end || line[end]) {
                replies.reply("CNTRL ERROR state-command"); return true;
            }
        }
        if (!decode(hex, path)) { replies.reply("CNTRL ERROR state-path"); return true; }
        auto file = filesystem_.file();
        if (!file || !file->open(path, files::Mode::read)) { replies.reply("CNTRL STATE MISSING"); return true; }
        if (size) {
            char response[64]; std::snprintf(response, sizeof(response), "CNTRL STATE %lu", (unsigned long)file->size());
            replies.reply(response); return true;
        }
        if (offset >= file->size()) { replies.reply("CNTRL ERROR state-offset"); return true; }
        const unsigned n = std::min(usb_block_size, unsigned(file->size() - offset));
        MemoryBuffer buffer;
        if (!buffer.resize(n) || !file->read(offset, buffer.data, n)) replies.reply("CNTRL ERROR state-read");
        else replies.data(buffer.data, n);
        return true;
    }
private:
    files::FileSystem &filesystem_;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(state_decode)
#endif
    static bool decode(const char *hex, char (&path)[224]) {
        const unsigned n = unsigned(std::strlen(hex));
        if (!n || n % 2 || n >= sizeof(path) * 2) return false;
        for (unsigned i = 0; i < n; i += 2) {
            unsigned value = 0;
            for (unsigned j = 0; j < 2; ++j) {
                char c = hex[i + j];
                if (c >= '0' && c <= '9') value = value * 16 + unsigned(c - '0');
                else if (c >= 'a' && c <= 'f') value = value * 16 + unsigned(c - 'a' + 10);
                else return false;
            }
            if (value < 32 || value > 126 || value == '\\') return false;
            path[i / 2] = char(value);
        }
        path[n / 2] = 0;
        if (std::strncmp(path, "/cntrl/", 7)) return false;
        for (const char *p = path; *p;) {
            if (*p == '/') { ++p; continue; }
            const char *end = std::strchr(p, '/'); unsigned length = end ? unsigned(end - p) : unsigned(std::strlen(p));
            if ((length == 1 && p[0] == '.') || (length == 2 && p[0] == '.' && p[1] == '.')) return false;
            p += length;
        }
        return true;
    }
};
} // namespace cntrl
