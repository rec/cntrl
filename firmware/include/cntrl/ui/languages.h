#pragma once
#include <cntrl/filesystem.h>
#include <cntrl/hardware.h>
#include <cntrl/usb_frame.h>
#include <cntrl/checked_array.h>
#include "fonts.h"
#include "messages.h"
#ifdef CNTRL_EMULATOR
#include <map>
#include <string>
#endif

namespace cntrl::ui {
// Read and validate one candidate without disturbing the visible catalogue.
// Each preparation call reads at most one sector or checks four messages.
class Languages {
public:
    explicit Languages(files::FileSystem &filesystem) : filesystem_(filesystem) {}
    ~Languages() {
        if (visible_ == this) {
            i18n::lookup = nullptr; AssetFonts::localized(nullptr, 0); visible_ = nullptr;
        }
    }
    const char *current() const { return tag_; }
#ifdef CNTRL_EMULATOR
    const char *current_name() const {
        return active_.size ? reinterpret_cast<const char *>(active_.data + 32) : "English";
    }
    static const char *original(i18n::Message message, bool short_form) {
        if (const char *value = pack_text(message, short_form)) return value;
        const auto &definition = i18n::definitions[unsigned(message)];
        return short_form ? definition.short_text : definition.text;
    }
    void correction(i18n::Message message, bool short_form, const std::string &value) {
        if (correction_language_ != tag_) corrections_.clear();
        correction_language_ = tag_;
        i18n::lookup = lookup;
        const unsigned field = unsigned(message) * 2 + short_form;
        if (value.empty() || value == original(message, short_form)) corrections_.erase(field);
        else corrections_[field] = value;
    }
    void clear_corrections() {
        corrections_.clear();
        i18n::lookup = active_.size ? lookup : nullptr;
    }
#endif
    bool available(const char *tag) const {
        if (!std::strcmp(tag, "en")) return true;
        char path[48]; filename(tag, path);
        return tag[0] && GlobalSettings::language_valid(tag) && filesystem_.exists(path);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(languages_count)
#endif
    unsigned count() const {
        installed_.release();
        Info english{}; std::strcpy(english.tag, "en"); std::strcpy(english.name, "English");
        if (!installed_.resize(1)) return 1;
        installed_[0] = english;
        auto directory = filesystem_.directory();
        if (!directory || !directory->open("/cntrl/languages")) return 1;
        char name[96]; bool folder;
        while (directory->next(name, sizeof(name), folder)) {
            const unsigned length = unsigned(std::strlen(name));
            if (folder || length < 5 || length > 19 || std::strcmp(name + length - 4, ".bin")) continue;
            Info info{}; std::memcpy(info.tag, name, length - 4);
            if (!std::strcmp(info.tag, "en") || !GlobalSettings::language_valid(info.tag)) continue;
            auto file = filesystem_.file(); char path[48]; filename(info.tag, path); uint8_t header[header_size];
            if (!file || !file->open(path, files::Mode::read) || file->size() > maximum
                || file->size() < header_size || !file->read(0, header, sizeof(header))
                || !valid_header(header, file->size(), info.tag)) continue;
            std::memcpy(info.name, header + 32, sizeof(info.name));
            const unsigned index = installed_.size();
            if (!installed_.resize(index + 1)) break;
            installed_[index] = info;
        }
        // Directory order varies by filesystem; keep recovery English first.
        for (unsigned i = 2; i < installed_.size(); ++i) {
            const auto value = installed_[i]; unsigned j = i;
            while (j > 1 && std::strcmp(installed_[j - 1].tag, value.tag) > 0) {
                installed_[j] = installed_[j - 1]; --j;
            }
            installed_[j] = value;
        }
        return installed_.size();
    }
    const char *tag(unsigned index) const { return index < installed_.size() ? installed_[index].tag : "en"; }
    const char *name(unsigned index) const {
        if (index >= installed_.size()) return "English";
        return installed_[index].name;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(languages_prepare)
#endif
    LanguagePreparation prepare(const char *tag) {
        using Result = LanguagePreparation;
        if (!tag[0] || !GlobalSettings::language_valid(tag)) return Result::failed;
        if (phase_ != Phase::idle && std::strcmp(candidate_tag_, tag)) finish(false);
        if (phase_ == Phase::ready) return Result::ready;
        if (phase_ == Phase::failed) return Result::failed;
        if (phase_ == Phase::idle) {
            std::snprintf(candidate_tag_, sizeof(candidate_tag_), "%s", tag);
            reader_ = filesystem_.file(); char path[48]; filename(tag, path);
            if (!reader_ || !reader_->open(path, files::Mode::read)) {
                if (!std::strcmp(tag, "en") && !filesystem_.exists(path)) {
                    phase_ = Phase::ready; return Result::ready;
                }
                return fail();
            }
            uint8_t header[header_size];
            if (reader_->size() < header_size || reader_->size() > maximum
                || !reader_->read(0, header, sizeof(header)) || !valid_header(header, reader_->size(), tag)) return fail();
            if (!std::strcmp(tag_, tag) && active_.size && u32(active_.data + 12) == u32(header + 12)) {
                reader_->close(); reuse_ = true; phase_ = Phase::ready; return Result::ready;
            }
            if (!pending_.resize(reader_->size())) return fail();
            std::memcpy(pending_.data, header, header_size); offset_ = header_size;
            crc_ = 0xffffffff; checksum(header, header_size, 0); index_ = 0; phase_ = Phase::reading;
            return Result::pending;
        }
        if (phase_ == Phase::reading) {
            unsigned length = std::min(512u, pending_.size - offset_);
            if (!reader_->read(offset_, pending_.data + offset_, length)) return fail();
            checksum(pending_.data + offset_, length, offset_); offset_ += length;
            if (offset_ == pending_.size) {
                reader_->close();
                if (~crc_ != u32(pending_.data + 12)) return fail();
                phase_ = Phase::messages;
            }
            return Result::pending;
        }
        if (phase_ == Phase::messages) {
            for (unsigned work = 0; work < 4 && index_ < i18n::message_count; ++work, ++index_) {
                const auto message = static_cast<i18n::Message>(index_);
                const auto *record = pending_.data + header_size + index_ * 8;
                for (unsigned form = 0; form < 2; ++form) {
                    unsigned at = u32(record + form * 4);
                    if (form && !at) continue;
                    if (!valid_string(at) || !i18n::valid_template(message, reinterpret_cast<const char *>(pending_.data + at))) return fail();
                }
            }
            if (index_ == i18n::message_count) { phase_ = Phase::fonts; offset_ = u32(pending_.data + 68) + 8; index_ = 0; }
            return Result::pending;
        }
        const unsigned start = u32(pending_.data + 68), length = u32(pending_.data + 72);
        if (!length) { phase_ = Phase::ready; return Result::ready; }
        if (length < 8 || std::memcmp(pending_.data + start, "CFN2", 4)) return fail();
        const unsigned count = u32(pending_.data + start + 4);
        if (!count || count > 32) return fail();
        if (index_ < count) {
            font_assets::Entry entry;
            if (offset_ > pending_.size || pending_.size - offset_ < sizeof(entry.bytes)) return fail();
            std::memcpy(entry.bytes, pending_.data + offset_, sizeof(entry.bytes)); offset_ += sizeof(entry.bytes);
            if (!entry.valid() || entry.length() > pending_.size - offset_
                || !font_assets::valid_pixels(entry, pending_.data + offset_)) return fail();
            offset_ += entry.length(); ++index_; return Result::pending;
        }
        if (offset_ != pending_.size || !AssetFonts::valid_localized(pending_.data + start, length)) return fail();
        phase_ = Phase::ready; return Result::ready;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(languages_finish)
#endif
    void finish(bool commit) {
        if (commit && phase_ == Phase::ready) {
            if (!reuse_) active_.swap(pending_);
            std::snprintf(tag_, sizeof(tag_), "%s", candidate_tag_);
            visible_ = this;
            i18n::lookup = active_.size ? lookup : nullptr;
            i18n::translated_ui = active_.size && std::strcmp(tag_, "en");
            AssetFonts::localized(active_.size && u32(active_.data + 72) ? active_.data + u32(active_.data + 68) : nullptr,
                active_.size ? u32(active_.data + 72) : 0);
        }
        if (reader_) reader_->close(); reader_.reset(); pending_.clear();
        phase_ = Phase::idle; reuse_ = false; candidate_tag_[0] = 0;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(languages_boot)
#endif
    bool boot(const char *tag) {
        auto result = prepare(tag && tag[0] ? tag : "en");
        while (result == LanguagePreparation::pending) result = prepare(tag && tag[0] ? tag : "en");
        if (result == LanguagePreparation::ready) { finish(true); return true; }
        finish(false); result = prepare("en");
        while (result == LanguagePreparation::pending) result = prepare("en");
        if (result == LanguagePreparation::ready) finish(true);
        else { finish(false); visible_ = this; i18n::lookup = nullptr; i18n::translated_ui = false; AssetFonts::localized(nullptr, 0); }
        return false;
    }
    static constexpr unsigned maximum = 262144, header_size = 76;
private:
    enum class Phase { idle, reading, messages, fonts, ready, failed };
    files::FileSystem &filesystem_;
    struct Info { char tag[16], name[32]; };
    mutable CheckedArray<Info> installed_;
    std::unique_ptr<files::File> reader_;
    MemoryBuffer active_, pending_;
    char tag_[16] = "en", candidate_tag_[16] = {};
    Phase phase_ = Phase::idle;
    unsigned offset_ = 0, index_ = 0;
    uint32_t crc_ = 0xffffffff;
    bool reuse_ = false;
    inline static Languages *visible_ = nullptr;
    static void filename(const char *tag, char (&path)[48]) { std::snprintf(path, sizeof(path), "/cntrl/languages/%s.bin", tag); }
    LanguagePreparation fail() { if (reader_) reader_->close(); pending_.clear(); phase_ = Phase::failed; return LanguagePreparation::failed; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(languages_header)
#endif
    static bool valid_header(const uint8_t *p, unsigned length, const char *tag) {
        return !std::memcmp(p, "CLNG", 4) && u16(p + 4) == 3 && u16(p + 6) == i18n::message_count
            && u32(p + 8) == length && u32(p + 64) == i18n::catalogue_schema
            && std::memchr(p + 16, 0, 16) && !std::strcmp(reinterpret_cast<const char *>(p + 16), tag)
            && p[32] && std::memchr(p + 32, 0, 32)
            && utf8_valid(p + 32, unsigned(std::strlen(reinterpret_cast<const char *>(p + 32))))
            && u32(p + 68) >= header_size + i18n::message_count * 8
            && u32(p + 68) <= length && u32(p + 72) == length - u32(p + 68);
    }
    bool valid_string(unsigned at) const {
        const unsigned end = u32(pending_.data + 68);
        if (at < header_size + i18n::message_count * 8 || at >= end) return false;
        const auto *nul = static_cast<const uint8_t *>(std::memchr(pending_.data + at, 0, std::min(512u, end - at)));
        return nul && nul != pending_.data + at && utf8_valid(pending_.data + at, unsigned(nul - pending_.data - at));
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(languages_checksum)
#endif
    void checksum(const uint8_t *bytes, unsigned length, unsigned offset) {
        for (unsigned i = 0; i < length; ++i) {
            const uint8_t byte = offset + i >= 12 && offset + i < 16 ? 0 : bytes[i];
            crc_ = (crc_ >> 8) ^ usb_crc_table[(crc_ ^ byte) & 255];
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(languages_lookup)
#endif
    static const char *lookup(i18n::Message message, bool short_form) {
#ifdef CNTRL_EMULATOR
        if (visible_ && visible_->correction_language_ == visible_->tag_) {
            const auto &overrides = visible_->corrections_;
            const auto found = overrides.find(unsigned(message) * 2 + short_form);
            if (found != overrides.end()) return found->second.c_str();
        }
#endif
        return pack_text(message, short_form);
    }
    static const char *pack_text(i18n::Message message, bool short_form) {
        if (!visible_ || !visible_->active_.size) return nullptr;
        const unsigned index = static_cast<unsigned>(message);
        if (index >= i18n::message_count) return nullptr;
        unsigned at = u32(visible_->active_.data + header_size + index * 8 + (short_form ? 4 : 0));
        return at ? reinterpret_cast<const char *>(visible_->active_.data + at) : nullptr;
    }
#ifdef CNTRL_EMULATOR
    std::string correction_language_;
    std::map<unsigned, std::string> corrections_;
#endif
};
} // namespace cntrl::ui
