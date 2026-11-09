#pragma once
#include <cntrl/usb_config.h>
#include <cntrl/ui/font_assets.h>
#include <cntrl/documents.h>
#include <cntrl/filesystem.h>

namespace font_assets {
class Storage {
public:
    Storage(cntrl::files::FileSystem &filesystem, cntrl::files::File &reader, cntrl::files::File &writer)
        : filesystem_(filesystem), reader_(reader), writer_(writer) {}
    bool ready = false;
    void keepalive(uint32_t now) { last_ = now; }
    void cancel_transfer() { writer_.close(); }
    bool busy() const { return writer_.opened(); }
    void poll(uint32_t now, cntrl::Replies &replies) {
        if (writer_.opened() && uint32_t(now - last_) > 10000) {
            writer_.close(); replies.reply("CNTRL ERROR font-timeout");
        }
    }
    bool supports(uint32_t key, unsigned size) const {
        Close close{reader_};
        bool opened = open(key); auto &file = reader_; Entry entry; unsigned offset;
        return opened && find(file, size, entry, offset);
    }
    bool stage(uint32_t key, unsigned size) {
        if (cache.contains(key, size)) return true;
        Close close{reader_};
        bool opened = open(key); auto &file = reader_; Entry entry; unsigned offset;
        if (!opened || !find(file, size, entry, offset) || !cache.fits()
            || (!cache.verified(key) && !verify(file, key))) return false;
        if (cache.reuse(key, size)) return true;
        cntrl::MemoryBuffer data;
        return data.resize(entry.length()) && file.read(offset, data.data, entry.length())
            && cache.add(key, entry, std::move(data));
    }
    bool command(const char *line, cntrl::Replies &replies, uint32_t now = 0) {
        if (std::strncmp(line, "CNTRL FONT ", 11)) return false;
        last_ = now; Close close{reader_};
        auto error = [&]() { writer_.close(); replies.reply("CNTRL ERROR font-asset"); return true; };
        if (!ready) return error();
        unsigned long key = 0; unsigned length = 0; int end = 0;
        if (std::sscanf(line, "CNTRL FONT BEGIN %lx %u%n", &key, &length, &end) == 2 && end && !line[end]
            && key && key <= 0xfffffffful && length >= 8 && length <= max_file) {
            writer_.close();
            bool existing = open(uint32_t(key));
            if (existing) {
                if (unsigned(reader_.size()) != length || !verify(reader_, uint32_t(key))) return error();
                replies.reply("CNTRL EXISTS"); return true;
            }
            if (!filesystem_.exists("/cntrl/fonts") && !filesystem_.make_directory("/cntrl/fonts")) return error();
            if (!writer_.open(temporary_, cntrl::files::Mode::truncate)) return error();
            expected_ = length; received_ = 0; key_ = uint32_t(key);
            replies.reply("CNTRL SEND"); return true;
        }
        if (!std::strncmp(line, "CNTRL FONT DATA ", 16)) {
            unsigned n = unsigned(std::strlen(line + 16));
            cntrl::UsbBlock block; if (!block.bytes) return error();
            auto *bytes = block.bytes;
            if (!writer_.opened() || !n || n % 2 || n > cntrl::usb_block_size * 2 || n / 2 > expected_ - received_) return error();
            for (unsigned i = 0; i < n; ++i) {
                char c = line[16 + i]; int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (digit < 0) return error();
                if (i % 2) bytes[i / 2] |= uint8_t(digit); else bytes[i / 2] = uint8_t(digit << 4);
            }
            if (!writer_.write(received_, bytes, n / 2)) { writer_.close(); return error(); }
            received_ += n / 2; replies.reply("CNTRL OK"); return true;
        }
        if (!std::strcmp(line, "CNTRL FONT END")) {
            bool good = writer_.opened() && received_ == expected_ && writer_.sync(); writer_.close();
            auto &file = reader_;
            bool opened = file.open(temporary_, cntrl::files::Mode::read);
            if (!good || !opened || !verify(file, key_) || !validate(file)) return error();
            file.close(); char path[48]; filename(key_, path);
            if (filesystem_.exists(path) || !filesystem_.rename(temporary_, path)) return error();
            replies.reply("CNTRL OK"); return true;
        }
        end = 0;
        if (std::sscanf(line, "CNTRL FONT SIZE %lx%n", &key, &end) == 1 && end && !line[end] && key && key <= 0xfffffffful) {
            auto &file = reader_; if (!open(uint32_t(key))) return error();
            char response[64]; std::snprintf(response, sizeof(response), "CNTRL FONT %lu", (unsigned long)file.size());
            replies.reply(response); return true;
        }
        end = 0;
        if (std::sscanf(line, "CNTRL FONT READ %lx %u%n", &key, &length, &end) == 2 && end && !line[end] && key && key <= 0xfffffffful) {
            auto &file = reader_; cntrl::UsbBlock block;
            if (!block.bytes) return error();
            auto *bytes = block.bytes;
            if (!open(uint32_t(key)) || length >= file.size()) return error();
            unsigned n = unsigned(file.size() - length); if (n > cntrl::usb_block_size) n = cntrl::usb_block_size;
            if (!file.read(length, bytes, n)) return error();
            char *response = block.text; std::strcpy(response, "CNTRL DATA ");
            for (unsigned i = 0; i < n; ++i) std::snprintf(response + 11 + i * 2, 3, "%02x", bytes[i]);
            replies.reply(response); return true;
        }
        return error();
    }
private:
    struct Close { cntrl::files::File &file; ~Close() { file.close(); } };
    cntrl::files::FileSystem &filesystem_;
    cntrl::files::File &reader_;
    cntrl::files::File &writer_;
    unsigned expected_ = 0, received_ = 0;
    uint32_t key_ = 0;
    uint32_t last_ = 0;
    const char *temporary_ = "/cntrl/fonts/upload.tmp";
    static void filename(uint32_t key, char *path) { std::snprintf(path, 48, "/cntrl/fonts/%08lx.cfont", (unsigned long)key); }
    bool open(uint32_t key) const {
        if (!ready) return false;
        char path[48]; filename(key, path); return reader_.open(path, cntrl::files::Mode::read);
    }
    static bool header(cntrl::files::File &file, unsigned &count) {
        uint8_t bytes[8];
        if (file.size() < 8 || file.size() > max_file || !file.read(0, bytes, 8) || std::memcmp(bytes, "CFN2", 4)) return false;
        count = cntrl::u32(bytes + 4); return count && count <= 32;
    }
    static bool find(cntrl::files::File &file, unsigned size, Entry &result, unsigned &offset) {
        unsigned count; if (!header(file, count)) return false;
        unsigned cursor = 8; bool found = false; bool seen[256] = {};
        for (unsigned i = 0; i < count; ++i) {
            Entry entry;
            if (cursor + 23 > file.size() || !file.read(cursor, entry.bytes, 23)
                || !entry.valid() || seen[entry.size()] || entry.length() > file.size() - cursor - 23) return false;
            seen[entry.size()] = true;
            if (entry.size() == size) { result = entry; offset = cursor + 23; found = true; }
            cursor += 23 + entry.length();
        }
        return found && cursor == file.size();
    }
    static bool verify(cntrl::files::File &file, uint32_t key) {
        uint32_t crc = 0xffffffff; uint8_t bytes[512];
        unsigned remaining = unsigned(file.size());
        if (remaining < 8 || remaining > max_file) return false;
        while (remaining) {
            unsigned n = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
            if (!file.read(unsigned(file.size()) - remaining, bytes, n)) return false;
            for (unsigned i = 0; i < n; ++i) {
                crc ^= bytes[i];
                for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
            }
            remaining -= n;
        }
        return ~crc == key;
    }
    static bool validate(cntrl::files::File &file) {
        unsigned count; if (!header(file, count)) return false;
        unsigned cursor = 8; bool seen[256] = {};
        for (unsigned i = 0; i < count; ++i) {
            Entry entry;
            if (cursor + 23 > file.size() || !file.read(cursor, entry.bytes, 23)
                || !entry.valid() || seen[entry.size()] || entry.length() > file.size() - cursor - 23) return false;
            seen[entry.size()] = true;
            cntrl::MemoryBuffer data;
            if (!data.resize(entry.length()) || !file.read(cursor + 23, data.data, entry.length()) || !valid_pixels(entry, data.data)) return false;
            cursor += 23 + entry.length();
        }
        return cursor == file.size();
    }
};
} // namespace font_assets
