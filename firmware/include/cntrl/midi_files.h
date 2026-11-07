#pragma once
#include <charconv>
#include <cntrl/filesystem.h>

namespace cntrl::midi {
class CachedMidiFile : public cntrl::midi::MidiFile {
public:
    explicit CachedMidiFile(files::File &file) : file_(file) {}
    void close() { file_.close(); invalidate(); }
    bool open(const char *path, files::Mode mode) { invalidate(); return file_.open(path, mode); }
    bool opened() const { return file_.opened(); }
    void invalidate() { cached_ = false; }
    uint32_t size() const override { return file_.size(); }
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        if (!file_.opened() || offset > size() || length > size() - offset) return false;
        for (unsigned i = 0; i < length; ++i) {
            uint32_t address = offset + i, block = address & ~511u;
            if (!cached_ || block != cached_offset_) {
                if (cache_.empty()) cache_.resize(512);
                cached_offset_ = block;
                unsigned n = std::min(uint32_t(512), size() - block);
                if (!file_.read(block, cache_.data(), n)) return false;
                cached_ = true;
            }
            data[i] = cache_[address - cached_offset_];
        }
        return true;
    }
    bool write(uint32_t offset, const uint8_t *data, unsigned length) override {
        cached_ = false;
        return file_.write(offset, data, length);
    }
    bool sync() override { return file_.sync(); }
private:
    files::File &file_;
    std::vector<uint8_t> cache_;
    uint32_t cached_offset_ = 0;
    bool cached_ = false;
};

class MidiFiles {
    CachedMidiFile source_, index_, recording_;
public:
    MidiFiles(files::FileSystem &filesystem, files::File &source, files::File &index,
              files::File &recording, files::Directory &entries, unsigned inputs,
              const char *directory, const char *index_path)
        : source_(source), index_(index), recording_(recording), filesystem_(filesystem),
          entries_(entries), parsers_(inputs), directory_(directory), index_path_(index_path) {}
    cntrl::midi::MidiFilePlayer player{source_, index_};
    cntrl::midi::MidiFileRecorder recorder{recording_};
    bool play(unsigned invocation, const cntrl::midi::PlayMidiFilePatch &settings) {
        if (invocation >= cntrl::midi::Bridge::owners || player.active() || player.cleaning()) return false;
        const char *filename = settings.filename[0] ? settings.filename : latest_;
        if (!filename[0] || (recorder.active() && !std::strcmp(filename, recording_name_))) return false;
        char path[128]; std::snprintf(path, sizeof(path), "%s/%s", directory_, filename);
        source_.close(); index_.close();
        source_.open(path, files::Mode::read);
        index_.open(index_path_, files::Mode::truncate);
        if (!source_.opened() || !index_.opened()) return false;
        return player.start(settings, invocation);
    }
    bool record(const cntrl::midi::RecordMidiFilePatch &settings, uint64_t start_count, bool start_counter_ready, uint32_t now) {
        if (recorder.active()) {
            bool ok = recorder.finish(now); recording_.close();
            if (ok) std::strcpy(latest_, recording_name_);
            return ok;
        }
        if (!start_counter_ready
            || (!filesystem_.exists(directory_) && !filesystem_.make_directory(directory_))) return false;
        if (count_ == UINT32_MAX) return false;
        char number[21]; *std::to_chars(number, number + 20, start_count).ptr = 0;
        std::snprintf(recording_name_, sizeof(recording_name_), "%s-%lu.mid", number, (unsigned long)++count_);
        char path[128]; std::snprintf(path, sizeof(path), "%s/%s", directory_, recording_name_);
        recording_.close(); recording_.open(path, files::Mode::exclusive);
        for (auto &parser : parsers_) parser.reset();
        return recording_.opened() && recorder.start(settings, now);
    }
    void receive(unsigned input, const cntrl::midi::Packet &packet, uint32_t now) {
        if (!recorder.active()) return;
        for (unsigned i = 0; i < packet.size(); ++i) receive(input, packet.bytes[i], now);
    }
    void receive(unsigned input, uint8_t byte, uint32_t now) {
        if (!recorder.active() || input >= parsers_.size()) return;
        cntrl::midi::Packet packet;
        if (parsers_[input].receive(byte, packet)) recorder.receive(input, packet, now);
    }
    void boot() {
        latest_[0] = 0;
        if (!entries_.open(directory_)) return;
        uint64_t latest_start = 0; unsigned latest_count = 0;
        char name[96] = {};
        bool is_directory = false;
        while (entries_.next(name, sizeof(name), is_directory)) {
            uint64_t start = 0; unsigned count = 0;
            const char *end = name + std::strlen(name);
            auto first = std::from_chars(name, end, start);
            if (first.ec != std::errc{} || first.ptr == end || *first.ptr != '-' || !start) { continue; }
            auto second = std::from_chars(first.ptr + 1, end, count);
            if (second.ec != std::errc{} || std::strcmp(second.ptr, ".mid") || !count
                || start < latest_start || (start == latest_start && count <= latest_count)) { continue; }
            if (is_directory) continue;
            char path[128]; std::snprintf(path, sizeof(path), "%s/%s", directory_, name);
            if (!source_.open(path, files::Mode::read)) continue;
            uint8_t header[22], tail[3];
            uint64_t size = source_.size();
            bool valid = size >= 26 && source_.read(0, header, sizeof(header))
                && !std::memcmp(header, "MThd", 4) && !std::memcmp(header + 14, "MTrk", 4)
                && (uint32_t(header[18]) << 24 | uint32_t(header[19]) << 16 | uint32_t(header[20]) << 8 | header[21]) == size - 22
                && source_.read(uint32_t(size - 3), tail, 3) && tail[0] == 0xff && tail[1] == 0x2f && !tail[2];
            source_.close();
            if (valid) {
                latest_start = start; latest_count = count; std::strcpy(latest_, name);
            }
        }
        entries_.close();
    }
private:
    files::FileSystem &filesystem_;
    files::Directory &entries_;
    std::vector<cntrl::midi::Parser> parsers_;
    char latest_[96] = {}, recording_name_[96] = {};
    uint32_t count_ = 0;
    const char *directory_;
    const char *index_path_;
};

} // namespace cntrl::midi
