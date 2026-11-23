#pragma once
#include "documents.h"
#include "global_settings.h"
#include "ui/messages.h"
#include <cerrno>
#include <cstdlib>

namespace cntrl {
// Configuration commands and switch actions share validation and persistence.
class GlobalCommands {
public:
    GlobalCommands(GlobalSettings &settings, Documents &documents, Hardware &hardware, Replies &replies)
        : settings_(settings), documents_(documents), hardware_(hardware), replies_(replies) {}
    void cancel() {
        pending_ = false;
        if (language_pending_) hardware_.finish_language(false);
        language_pending_ = language_reply_ = false;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_request_language)
#endif
    bool request_language(const char *tag, bool clear = false) {
        if (!GlobalSettings::language_valid(tag)) return false;
        GlobalSettings::Values values = settings_.values();
        std::snprintf(values.language, sizeof(values.language), "%s", clear ? "" : tag);
        return queue_language(values, false, false);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_request)
#endif
    bool request(unsigned field, uint32_t value, bool clear) {
        if (pending_ || language_pending_ || field >= GlobalSettings::count || (!clear && !GlobalSettings::valid(field, value))
            || (field >= 2 && hardware_.global_settings_busy())) return false;
        field_ = field; value_ = value; clear_ = clear; pending_ = true; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_poll)
#endif
    void poll(bool patch_pending) {
        if (language_pending_) {
            uint64_t numeric_changed = settings_.values().known ^ language_values_.known;
            for (unsigned i = 0; i < GlobalSettings::count; ++i)
                if (settings_.values().values[i] != language_values_.values[i]) numeric_changed |= uint64_t(1) << i;
            if (patch_pending && !language_staging_ && (numeric_changed & ~uint64_t(7))) {
                language_pending_ = false; hardware_.finish_language(false);
                if (language_reply_) error("global-busy");
                else hardware_.error(ui::i18n::text(ui::i18n::Message::language_change_rejected));
                language_reply_ = false; return;
            }
            const bool changed = std::strcmp(language_values_.language, settings_.values().language) != 0;
            const auto ready = hardware_.prepare_language(language_values_.language[0] ? language_values_.language : "en");
            if (ready == LanguagePreparation::pending) return;
            language_pending_ = false;
            bool success = ready == LanguagePreparation::ready;
            if (success) success = documents_.global_replace(language_values_, language_staging_ ? false : hardware_.global_settings_busy());
            if (success && !changed) hardware_.finish_language(true);
            if (!success) hardware_.finish_language(false);
            if (language_reply_) {
                if (success) replies_.reply("CNTRL OK");
                else error(ready == LanguagePreparation::failed ? "global-language" : documents_.last_error());
            } else if (!success) hardware_.error(ui::i18n::text(ui::i18n::Message::language_change_rejected));
            language_reply_ = false;
            return;
        }
        if (!pending_) return;
        pending_ = false;
        if ((GlobalSettings::reload(field_) && patch_pending)
            || !documents_.global_change(field_, value_, clear_, hardware_.global_settings_busy()))
            hardware_.error(ui::i18n::text(ui::i18n::Message::global_setting_rejected));
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
            if (values.language[0]) length += unsigned(std::snprintf(response + length, sizeof(response) - length, " language=%s", values.language));
            if (values.startup_set[0]) {
                length += unsigned(std::snprintf(response + length, sizeof(response) - length, " startup_set="));
                for (const char *p = values.startup_set; *p; ++p) length += unsigned(std::snprintf(response + length, sizeof(response) - length, "%02x", unsigned(uint8_t(*p))));
            }
            if (values.name_and_contact[0]) {
                length += unsigned(std::snprintf(response + length, sizeof(response) - length, " name_and_contact="));
                for (const char *p = values.name_and_contact; *p; ++p)
                    length += unsigned(std::snprintf(response + length, sizeof(response) - length, "%02x", unsigned(uint8_t(*p))));
            }
            replies_.reply(response); return true;
        }
        if (staging && std::strncmp(line, "CNTRL GLOBAL PUT", 16)) return error("global-staging");
        constexpr char contact_set[] = "CNTRL GLOBAL SET name_and_contact ";
        if (!std::strncmp(line, contact_set, sizeof(contact_set) - 1) || !std::strcmp(line, "CNTRL GLOBAL CLEAR name_and_contact")) {
            if (pending_ || language_pending_) return error("global-busy");
            auto values = settings_.values();
            const char *p = line[13] == 'S' ? line + sizeof(contact_set) - 1 : "";
            if (!unhex(p, values.name_and_contact) || *p) return error("global-contact");
            if (!documents_.global_replace(values, false)) return error(documents_.last_error());
            replies_.reply("CNTRL OK"); return true;
        }
        constexpr char language_set[] = "CNTRL GLOBAL SET language ";
        if (!std::strncmp(line, language_set, sizeof(language_set) - 1)) {
            const char *tag = line + sizeof(language_set) - 1;
            if (!tag[0] || !GlobalSettings::language_valid(tag)) return error("global-language");
            GlobalSettings::Values values = settings_.values();
            std::snprintf(values.language, sizeof(values.language), "%s", tag);
            if (!queue_language(values, true, staging)) return error("global-busy");
            return true;
        }
        if (!std::strcmp(line, "CNTRL GLOBAL CLEAR language")) {
            GlobalSettings::Values values = settings_.values(); values.language[0] = 0;
            if (!queue_language(values, true, staging)) return error("global-busy");
            return true;
        }
        unsigned field = 0; unsigned long value = 0; int end = 0;
        if (std::sscanf(line, "CNTRL GLOBAL SET %u %lu%n", &field, &value, &end) == 2 && end && !line[end] && value <= UINT32_MAX) {
            if (pending_ || language_pending_) return error("global-busy");
            if (!documents_.global_change(field, uint32_t(value), false, hardware_.global_settings_busy())) return error(documents_.last_error());
            replies_.reply("CNTRL OK"); return true;
        }
        end = 0;
        if (std::sscanf(line, "CNTRL GLOBAL CLEAR %u%n", &field, &end) == 1 && end && !line[end]) {
            if (pending_ || language_pending_) return error("global-busy");
            if (!documents_.global_change(field, 0, true, hardware_.global_settings_busy())) return error(documents_.last_error());
            replies_.reply("CNTRL OK"); return true;
        }
        if (!std::strncmp(line, "CNTRL GLOBAL PUT", 16) && (!line[16] || line[16] == ' ')) {
            GlobalSettings::Values values; const char *p = line + 16;
            std::memcpy(values.name_and_contact, settings_.values().name_and_contact, sizeof(values.name_and_contact));
            bool contact_seen = false;
            while (*p) {
                if (*p++ != ' ') return error("global-values");
                if (!std::strncmp(p, "startup_set=", 12)) {
                    if (values.startup_set[0]) return error("global-values");
                    p += 12;
                    if (!unhex(p, values.startup_set) || !values.startup_set[0]) return error("global-values");
                    for (const char *name = values.startup_set; *name; ++name)
                        if (*name < 32 || *name > 126) return error("global-values");
                    continue;
                }
                if (!std::strncmp(p, "name_and_contact=", 17)) {
                    if (contact_seen) return error("global-contact");
                    contact_seen = true; p += 17;
                    if (!unhex(p, values.name_and_contact)) return error("global-contact");
                    continue;
                }
                if (!std::strncmp(p, "language=", 9)) {
                    if (values.language[0]) return error("global-values");
                    p += 9; const char *end = p; while (*end && *end != ' ') ++end;
                    const unsigned n = unsigned(end - p);
                    if (!n || n >= sizeof(values.language)) return error("global-language");
                    std::memcpy(values.language, p, n); values.language[n] = 0;
                    if (!GlobalSettings::language_valid(values.language)) return error("global-language");
                    p = end; continue;
                }
                if (*p < '0' || *p > '9') return error("global-values");
                char *next; errno = 0; unsigned long id = std::strtoul(p, &next, 10);
                if (errno || *next != '=' || id >= GlobalSettings::count || (values.known & (uint64_t(1) << id))) return error("global-values");
                p = next + 1; if (*p < '0' || *p > '9') return error("global-values");
                errno = 0; unsigned long v = std::strtoul(p, &next, 10);
                if (errno || v > UINT32_MAX || (*next && *next != ' ')) return error("global-values");
                values.known |= uint64_t(1) << id; values.values[id] = uint32_t(v); p = next;
            }
            if (pending_ || language_pending_) return error("global-busy");
            if (std::strcmp(values.language, settings_.values().language)
                || std::strcmp(hardware_.current_language(), values.language[0] ? values.language : "en")) {
                if (!queue_language(values, true, staging)) return error("global-busy");
                return true;
            }
            if (!documents_.global_replace(values, staging ? false : hardware_.global_settings_busy())) return error(documents_.last_error());
            replies_.reply("CNTRL OK"); return true;
        }
        return error("global-command");
    }
private:
    template<unsigned size>
    static bool unhex(const char *&p, char (&text)[size]) {
        unsigned n = 0;
        while (*p && *p != ' ') {
            if (n == sizeof(text) - 1 || !p[1] || p[1] == ' ') return false;
            unsigned value = 0;
            for (unsigned i = 0; i < 2; ++i) {
                const char c = *p++;
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
                value = value * 16 + unsigned(c <= '9' ? c - '0' : c - 'a' + 10);
            }
            if (!value) return false;
            text[n++] = char(value);
        }
        std::memset(text + n, 0, sizeof(text) - n);
        return true;
    }
    GlobalSettings &settings_; Documents &documents_; Hardware &hardware_; Replies &replies_;
    unsigned field_ = 0; uint32_t value_ = 0;
    bool pending_ = false, clear_ = false;
    GlobalSettings::Values language_values_;
    bool language_pending_ = false, language_reply_ = false, language_staging_ = false;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_queue_language)
#endif
    bool queue_language(const GlobalSettings::Values &values, bool reply, bool staging) {
        if (pending_ || language_pending_ || !settings_.ready() || !GlobalSettings::valid(values)) return false;
        language_values_ = values; language_pending_ = true; language_reply_ = reply; language_staging_ = staging;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(global_commands_error)
#endif
    bool error(const char *message) {
        char text[128]; std::snprintf(text, sizeof(text), "CNTRL ERROR %s", message);
        replies_.reply(text); return true;
    }
};
} // namespace cntrl
