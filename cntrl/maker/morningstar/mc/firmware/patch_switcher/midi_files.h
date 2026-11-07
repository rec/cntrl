#pragma once
#include <Arduino.h>
#include <SD.h>
#include <charconv>
#include <cntrl/midi_file.h>
#include "startup_counter.h"

class SdMidiFile : public cntrl::midi::MidiFile {
public:
    FsFile file;
    void invalidate() { cached_ = false; }
    uint32_t size() const override { return const_cast<FsFile &>(file).size(); }
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        if (!file || offset > size() || length > size() - offset) return false;
        for (unsigned i = 0; i < length; ++i) {
            uint32_t address = offset + i, block = address & ~511u;
            if (!cached_ || block != cached_offset_) {
                if (cache_.empty()) cache_.resize(512);
                cached_offset_ = block;
                unsigned n = std::min(uint32_t(512), size() - block);
                if (!file.seekSet(block) || file.read(cache_.data(), n) != int(n)) return false;
                cached_ = true;
            }
            data[i] = cache_[address - cached_offset_];
        }
        return true;
    }
    bool write(uint32_t offset, const uint8_t *data, unsigned length) override {
        cached_ = false;
        return file && file.seekSet(offset) && file.write(data, length) == length;
    }
    bool sync() override { return file && file.sync(); }
private:
    std::vector<uint8_t> cache_;
    uint32_t cached_offset_ = 0;
    bool cached_ = false;
};

class MidiFiles {
    SdMidiFile source_, index_, recording_;
public:
    cntrl::midi::MidiFilePlayer player{source_, index_};
    cntrl::midi::MidiFileRecorder recorder{recording_};
    CNTRL_FLASH_ROUTINE(midi_files_play)
    bool play(unsigned invocation, const cntrl::midi::PlayMidiFilePatch &settings) {
        if (invocation >= cntrl::midi::Bridge::owners || player.active() || player.cleaning()) return false;
        const char *filename = settings.filename[0] ? settings.filename : latest_;
        if (!filename[0] || (recorder.active() && !std::strcmp(filename, recording_name_))) return false;
        char path[128]; std::snprintf(path, sizeof(path), "%s/%s", directory, filename);
        source_.file.close(); index_.file.close(); source_.invalidate(); index_.invalidate();
        source_.file = SD.sdfs.open(path, O_RDONLY);
        index_.file = SD.sdfs.open(index_path, O_CREAT | O_TRUNC | O_RDWR);
        if (!source_.file || !index_.file) return false;
        return player.start(settings, invocation);
    }
    CNTRL_FLASH_ROUTINE(midi_files_record)
    bool record(const cntrl::midi::RecordMidiFilePatch &settings, const StartupCounter &starts, uint32_t now) {
        if (recorder.active()) {
            bool ok = recorder.finish(now); recording_.file.close();
            if (ok) std::strcpy(latest_, recording_name_);
            return ok;
        }
        if (starts.status != StartupCounter::Status::ready
            || (!SD.sdfs.exists(directory) && !SD.sdfs.mkdir(directory, true))) return false;
        if (count_ == UINT32_MAX) return false;
        std::snprintf(recording_name_, sizeof(recording_name_), "%llu-%lu.mid", (unsigned long long)starts.count, (unsigned long)++count_);
        char path[128]; std::snprintf(path, sizeof(path), "%s/%s", directory, recording_name_);
        recording_.file.close(); recording_.invalidate(); recording_.file = SD.sdfs.open(path, O_CREAT | O_EXCL | O_RDWR);
        for (auto &parser : parsers_) parser.reset();
        return recording_.file && recorder.start(settings, now);
    }
    void receive(unsigned input, const cntrl::midi::Packet &packet, uint32_t now) {
        if (!recorder.active()) return;
        for (unsigned i = 0; i < packet.size(); ++i) receive(input, packet.bytes[i], now);
    }
    void receive(unsigned input, uint8_t byte, uint32_t now) {
        if (!recorder.active()) return;
        cntrl::midi::Packet packet;
        if (parsers_[input].receive(byte, packet)) recorder.receive(input, packet, now);
    }
    CNTRL_FLASH_ROUTINE(midi_files_boot)
    void boot() {
        latest_[0] = 0;
        auto directory_file = SD.sdfs.open(directory, O_RDONLY);
        if (!directory_file || !directory_file.isDirectory()) return;
        uint64_t latest_start = 0; unsigned latest_count = 0;
        FsFile entry;
        while (entry.openNext(&directory_file, O_RDONLY)) {
            char name[96] = {};
            if (!entry.getName(name, sizeof(name))) { entry.close(); continue; }
            uint64_t start = 0; unsigned count = 0;
            const char *end = name + std::strlen(name);
            auto first = std::from_chars(name, end, start);
            if (first.ec != std::errc{} || first.ptr == end || *first.ptr != '-' || !start) { entry.close(); continue; }
            auto second = std::from_chars(first.ptr + 1, end, count);
            if (second.ec != std::errc{} || std::strcmp(second.ptr, ".mid") || !count
                || start < latest_start || (start == latest_start && count <= latest_count)) { entry.close(); continue; }
            uint8_t header[22], tail[3];
            uint64_t size = entry.size();
            bool valid = !entry.isDirectory() && size >= 26 && entry.read(header, sizeof(header)) == sizeof(header)
                && !std::memcmp(header, "MThd", 4) && !std::memcmp(header + 14, "MTrk", 4)
                && (uint32_t(header[18]) << 24 | uint32_t(header[19]) << 16 | uint32_t(header[20]) << 8 | header[21]) == size - 22
                && entry.seekSet(size - 3) && entry.read(tail, 3) == 3 && tail[0] == 0xff && tail[1] == 0x2f && !tail[2];
            entry.close();
            if (valid) {
                latest_start = start; latest_count = count; std::strcpy(latest_, name);
            }
        }
    }
private:
    cntrl::midi::Parser parsers_[9];
    char latest_[96] = {}, recording_name_[96] = {};
    uint32_t count_ = 0;
    static constexpr const char *directory = "/cntrl/midi";
    static constexpr const char *index_path = "/cntrl/midi/.playback-index";
};
