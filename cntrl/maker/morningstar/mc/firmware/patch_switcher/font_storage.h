#pragma once
#include "font_asset.h"
#include <cntrl/documents.h>
#include <SD.h>
#include <new>

namespace font_assets {
class Files {
public:
    bool ready = false;
    bool supports(uint32_t key, unsigned size) const {
        auto file = open(key); Entry entry; unsigned offset;
        return file && find(file, size, entry, offset);
    }
    bool stage(uint32_t key, unsigned size) {
        if (cache.contains(key, size)) return true;
        auto file = open(key); Entry entry; unsigned offset;
        if (!file || !verify(file, key) || !find(file, size, entry, offset)) return false;
        std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[entry.length()]);
        return data && file.seekSet(offset) && file.read(data.get(), entry.length()) == int(entry.length())
            && cache.add(key, entry, std::move(data));
    }
    bool command(const char *line, cntrl::Replies &replies) {
        if (std::strncmp(line, "CNTRL FONT ", 11)) return false;
        auto error = [&]() { replies.reply("CNTRL ERROR font-asset"); return true; };
        if (!ready) return error();
        unsigned long key = 0; unsigned length = 0; int end = 0;
        if (std::sscanf(line, "CNTRL FONT BEGIN %lx %u%n", &key, &length, &end) == 2 && end && !line[end]
            && key && key <= 0xfffffffful && length >= 8 && length <= max_file) {
            writer_.close();
            auto existing = open(uint32_t(key));
            if (existing) {
                if (unsigned(existing.size()) != length || !verify(existing, uint32_t(key))) return error();
                replies.reply("CNTRL EXISTS"); return true;
            }
            if (!SD.sdfs.exists("/cntrl/fonts") && !SD.sdfs.mkdir("/cntrl/fonts", true)) return error();
            writer_ = SD.sdfs.open(temporary_, O_RDWR | O_CREAT | O_TRUNC);
            if (!writer_) return error();
            expected_ = length; received_ = 0; key_ = uint32_t(key);
            replies.reply("CNTRL SEND"); return true;
        }
        if (!std::strncmp(line, "CNTRL FONT DATA ", 16)) {
            unsigned n = std::strlen(line + 16); uint8_t bytes[128];
            if (!writer_ || !n || n % 2 || n > 256 || n / 2 > expected_ - received_) return error();
            for (unsigned i = 0; i < n; ++i) {
                char c = line[16 + i]; int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (digit < 0) return error();
                if (i % 2) bytes[i / 2] |= uint8_t(digit); else bytes[i / 2] = uint8_t(digit << 4);
            }
            if (writer_.write(bytes, n / 2) != n / 2) { writer_.close(); return error(); }
            received_ += n / 2; replies.reply("CNTRL OK"); return true;
        }
        if (!std::strcmp(line, "CNTRL FONT END")) {
            bool good = writer_ && received_ == expected_ && writer_.sync(); writer_.close();
            auto file = SD.sdfs.open(temporary_, O_RDONLY);
            if (!good || !file || !verify(file, key_) || !validate(file)) return error();
            file.close(); char path[48]; filename(key_, path);
            if (SD.sdfs.exists(path) || !SD.sdfs.rename(temporary_, path)) return error();
            replies.reply("CNTRL OK"); return true;
        }
        end = 0;
        if (std::sscanf(line, "CNTRL FONT SIZE %lx%n", &key, &end) == 1 && end && !line[end] && key && key <= 0xfffffffful) {
            auto file = open(uint32_t(key)); if (!file) return error();
            char response[64]; std::snprintf(response, sizeof(response), "CNTRL FONT %lu", (unsigned long)file.size());
            replies.reply(response); return true;
        }
        end = 0;
        if (std::sscanf(line, "CNTRL FONT READ %lx %u%n", &key, &length, &end) == 2 && end && !line[end] && key && key <= 0xfffffffful) {
            auto file = open(uint32_t(key)); uint8_t bytes[128];
            if (!file || length >= file.size() || !file.seekSet(length)) return error();
            int n = file.read(bytes, sizeof(bytes)); if (n <= 0) return error();
            char response[280] = "CNTRL DATA ";
            for (int i = 0; i < n; ++i) std::snprintf(response + 11 + i * 2, 3, "%02x", bytes[i]);
            replies.reply(response); return true;
        }
        return error();
    }
private:
    FsFile writer_;
    unsigned expected_ = 0, received_ = 0;
    uint32_t key_ = 0;
    const char *temporary_ = "/cntrl/fonts/upload.tmp";
    static void filename(uint32_t key, char *path) { std::snprintf(path, 48, "/cntrl/fonts/%08lx.cfont", (unsigned long)key); }
    FsFile open(uint32_t key) const {
        if (!ready) return FsFile();
        char path[48]; filename(key, path); return SD.sdfs.open(path, O_RDONLY);
    }
    static bool header(FsFile &file, unsigned &count) {
        uint8_t bytes[8];
        if (file.size() < 8 || file.size() > max_file || !file.seekSet(0) || file.read(bytes, 8) != 8 || std::memcmp(bytes, "CFN1", 4)) return false;
        count = cntrl::u32(bytes + 4); return count && count <= 32;
    }
    static bool find(FsFile &file, unsigned size, Entry &result, unsigned &offset) {
        unsigned count; if (!header(file, count)) return false;
        unsigned cursor = 8; bool found = false; bool seen[256] = {};
        for (unsigned i = 0; i < count; ++i) {
            Entry entry;
            if (cursor + 23 > file.size() || !file.seekSet(cursor) || file.read(entry.bytes, 23) != 23
                || !entry.valid() || seen[entry.size()] || entry.length() > file.size() - cursor - 23) return false;
            seen[entry.size()] = true;
            if (entry.size() == size) { result = entry; offset = cursor + 23; found = true; }
            cursor += 23 + entry.length();
        }
        return found && cursor == file.size();
    }
    static bool verify(FsFile &file, uint32_t key) {
        if (!file.seekSet(0)) return false;
        uint32_t crc = 0xffffffff; uint8_t bytes[512];
        unsigned remaining = unsigned(file.size());
        if (remaining < 8 || remaining > max_file) return false;
        while (remaining) {
            unsigned n = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
            if (file.read(bytes, n) != int(n)) return false;
            for (unsigned i = 0; i < n; ++i) {
                crc ^= bytes[i];
                for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
            }
            remaining -= n;
        }
        return ~crc == key;
    }
    static bool validate(FsFile &file) {
        unsigned count; if (!header(file, count)) return false;
        unsigned cursor = 8; bool seen[256] = {};
        for (unsigned i = 0; i < count; ++i) {
            Entry entry;
            if (cursor + 23 > file.size() || !file.seekSet(cursor) || file.read(entry.bytes, 23) != 23
                || !entry.valid() || seen[entry.size()] || entry.length() > file.size() - cursor - 23) return false;
            seen[entry.size()] = true;
            std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[entry.length()]);
            if (!data || file.read(data.get(), entry.length()) != int(entry.length()) || !valid_pixels(entry, data.get())) return false;
            cursor += 23 + entry.length();
        }
        return cursor == file.size();
    }
};
} // namespace font_assets
