#pragma once
#include "documents.h"
#include "global_settings.h"
#include <cerrno>
#include <cstdlib>

namespace cntrl {
// Configuration commands and switch actions share validation and persistence.
class GlobalCommands {
public:
    GlobalCommands(GlobalSettings &settings, Documents &documents, Hardware &hardware, Replies &replies)
        : settings_(settings), documents_(documents), hardware_(hardware), replies_(replies) {}
    void cancel() { pending_ = false; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_request)
#endif
    bool request(unsigned field, uint32_t value, bool clear) {
        if (pending_ || field >= GlobalSettings::count || (!clear && !GlobalSettings::valid(field, value))
            || (field >= 2 && hardware_.global_settings_busy())) return false;
        field_ = field; value_ = value; clear_ = clear; pending_ = true; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_poll)
#endif
    void poll(bool patch_pending) {
        if (!pending_) return;
        pending_ = false;
        if ((GlobalSettings::reload(field_) && patch_pending)
            || !documents_.global_change(field_, value_, clear_, hardware_.global_settings_busy()))
            hardware_.error("Global setting rejected");
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_command)
#endif
    bool command(const char *line, bool staging = false) {
        if (std::strncmp(line, "CNTRL GLOBAL ", 13)) return false;
        if (!std::strcmp(line, "CNTRL GLOBAL GET")) {
            if (!settings_.ready()) return error("global-storage");
            char response[768] = "CNTRL GLOBAL"; unsigned length = 12;
            const auto &values = settings_.values();
            for (unsigned i = 0; i < GlobalSettings::count; ++i) if (values.known & (uint64_t(1) << i))
                length += unsigned(std::snprintf(response + length, sizeof(response) - length, " %u=%lu", i, (unsigned long)values.values[i]));
            replies_.reply(response); return true;
        }
        if (staging && std::strncmp(line, "CNTRL GLOBAL PUT", 16)) return error("global-staging");
        unsigned field = 0; unsigned long value = 0; int end = 0;
        if (std::sscanf(line, "CNTRL GLOBAL SET %u %lu%n", &field, &value, &end) == 2 && end && !line[end] && value <= UINT32_MAX) {
            if (pending_) return error("global-busy");
            if (!documents_.global_change(field, uint32_t(value), false, hardware_.global_settings_busy())) return error(documents_.last_error());
            replies_.reply("CNTRL OK"); return true;
        }
        end = 0;
        if (std::sscanf(line, "CNTRL GLOBAL CLEAR %u%n", &field, &end) == 1 && end && !line[end]) {
            if (pending_) return error("global-busy");
            if (!documents_.global_change(field, 0, true, hardware_.global_settings_busy())) return error(documents_.last_error());
            replies_.reply("CNTRL OK"); return true;
        }
        if (!std::strncmp(line, "CNTRL GLOBAL PUT", 16) && (!line[16] || line[16] == ' ')) {
            GlobalSettings::Values values; const char *p = line + 16;
            while (*p) {
                if (*p++ != ' ' || *p < '0' || *p > '9') return error("global-values");
                char *next; errno = 0; unsigned long id = std::strtoul(p, &next, 10);
                if (errno || *next != '=' || id >= GlobalSettings::count || (values.known & (uint64_t(1) << id))) return error("global-values");
                p = next + 1; if (*p < '0' || *p > '9') return error("global-values");
                errno = 0; unsigned long v = std::strtoul(p, &next, 10);
                if (errno || v > UINT32_MAX || (*next && *next != ' ')) return error("global-values");
                values.known |= uint64_t(1) << id; values.values[id] = uint32_t(v); p = next;
            }
            if (pending_) return error("global-busy");
            if (!documents_.global_replace(values, staging ? false : hardware_.global_settings_busy())) return error(documents_.last_error());
            replies_.reply("CNTRL OK"); return true;
        }
        return error("global-command");
    }
private:
    GlobalSettings &settings_; Documents &documents_; Hardware &hardware_; Replies &replies_;
    unsigned field_ = 0; uint32_t value_ = 0;
    bool pending_ = false, clear_ = false;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_error)
#endif
    bool error(const char *message) {
        char text[128]; std::snprintf(text, sizeof(text), "CNTRL ERROR %s", message);
        replies_.reply(text); return true;
    }
};
} // namespace cntrl
