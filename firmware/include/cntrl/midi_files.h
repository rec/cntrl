#pragma once
#include <cstdio>
#include <charconv>
#include <cntrl/filesystem.h>
#include <cntrl/midi_looper.h>

namespace cntrl::midi {
class CachedMidiFile : public cntrl::midi::MidiFile {
public:
    explicit CachedMidiFile(files::File &file) : file_(file) {}
    void close() { file_.close(); invalidate(); }
    bool open(const char *path, files::Mode mode) { invalidate(); return file_.open(path, mode); }
    bool opened() const { return file_.opened(); }
    void invalidate() { cached_ = false; }
    uint32_t size() const override { return file_.size(); }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(cached_midi_read)
#endif
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

class StreamFilePlayer : public StreamFileSource {
public:
    StreamFilePlayer(files::FileSystem &filesystem, std::unique_ptr<files::File> source,
                     std::unique_ptr<files::File> index, const char *index_path)
        : filesystem_(filesystem), source_file_(std::move(source)), index_file_(std::move(index)),
          source_(*source_file_), index_(*index_file_), player_(source_, index_) {
        std::snprintf(index_path_, sizeof(index_path_), "%s", index_path);
    }
    ~StreamFilePlayer() override {
        player_.cancel(); source_.close(); index_.close();
        if (opened_index_) filesystem_.remove(index_path_);
    }
    bool start(const char *path, PlayMidiFilePatch settings) {
        settings.outputs = 1;
        if (!source_.open(path, files::Mode::read)) return false;
        opened_index_ = index_.open(index_path_, files::Mode::truncate);
        return opened_index_ && player_.start(settings);
    }
    void poll(uint32_t now, Sink &sink) override { player_.poll(now, 1, sink); }
    bool failed() const override { return player_.status() == MidiFilePlayer::Status::failed; }
    bool finished() const override { return player_.status() == MidiFilePlayer::Status::complete && !player_.cleaning(); }
    bool clock() const override { return player_.clock_outputs() != 0; }
private:
    files::FileSystem &filesystem_;
    std::unique_ptr<files::File> source_file_, index_file_;
    CachedMidiFile source_, index_;
    MidiFilePlayer player_;
    char index_path_[160] = {};
    bool opened_index_ = false;
};

class MidiFiles : public StreamFileProvider {
    CachedMidiFile source_, index_, recording_;
public:
    MidiFiles(files::FileSystem &filesystem, files::File &source, files::File &index,
              files::File &recording, files::Directory &entries, unsigned inputs,
              const char *directory, const char *index_path)
        : source_(source), index_(index), recording_(recording), filesystem_(filesystem),
          entries_(entries), parsers_(inputs), directory_(directory), index_path_(index_path) {}
    cntrl::midi::MidiFilePlayer player{source_, index_};
    cntrl::midi::MidiFileRecorder recorder{recording_};
    MidiLooper looper{player,recorder};
    bool storage_busy() const { return saving_ || loading_ || browsing_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_save_loop)
#endif
    bool save_loop(unsigned tempo, uint64_t start_count, bool counter_ready) {
        if (storage_busy() || !tempo || tempo > 0xffffff) return false;
        auto *snapshot = looper.save_begin();
        if (!snapshot) return false;
        if (!open_recording(start_count,counter_ready)) { looper.save_complete(false); return false; }
        saving_ = snapshot; save_offset_ = save_stage_ = 0; save_tempo_ = tempo; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_load_loop)
#endif
    bool load_loop(const char *filename) {
        if (storage_busy() || !basename(filename)) return false;
        if (!looper.owned() && (player.active() || player.cleaning() || recorder.active())) return false;
        char path[128];
        int n = std::snprintf(path,sizeof(path),"%s/%s",directory_,filename);
        if (n < 0 || unsigned(n) >= sizeof(path) || !source_.open(path,files::Mode::read)) return false;
        if (!looper.load_begin(source_)) { source_.close(); return false; }
        loading_ = true; return true;
    }
    const char *loop_filename() const { return selected_; }
    bool browsing() const { return browsing_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_browse_begin)
#endif
    bool browse_loop(bool reverse, bool reset = false) {
        if (!looper.owned() && (player.active() || player.cleaning() || recorder.active())) return false;
        if (storage_busy() || !entries_.open(directory_)) return false;
        if (reset) selected_[0] = 0;
        browse_best_[0] = browse_wrap_[0] = 0; browse_reverse_ = reverse; browsing_ = true; return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_browse_cancel)
#endif
    void cancel_browse() { if (browsing_) entries_.close(); browsing_ = false; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_loop_storage)
#endif
    void loop_storage_poll() {
        if (loading_ && looper.state() != MidiLooper::State::loading) { source_.close(); loading_ = false; }
        if (browsing_) browse_step();
        if (!saving_) return;
        bool ok = filesystem_.available();
        if (ok && save_offset_ < saving_->size()) {
            uint8_t data[512];
            unsigned n = std::min(uint32_t(sizeof(data)),saving_->size() - save_offset_);
            ok = saving_->read(save_offset_,data,n);
            // Keep the incomplete file undiscoverable by ordinary boot checks.
            if (!save_offset_ && n >= 29) {
                std::memset(data + 18,0,4);
                data[26] = uint8_t(save_tempo_ >> 16); data[27] = uint8_t(save_tempo_ >> 8); data[28] = uint8_t(save_tempo_);
            }
            if (ok) ok = recording_.write(save_offset_,data,n);
            if (ok) { save_offset_ += n; return; }
        } else if (ok && save_stage_ == 0) { ok = recording_.sync(); if (ok) { ++save_stage_; return; } }
        else if (ok && save_stage_ == 1) {
            uint8_t length[4]; ok = saving_->read(18,length,4) && recording_.write(18,length,4);
            if (ok) { ++save_stage_; return; }
        } else if (ok) ok = recording_.sync();
        recording_.close();
        if (ok) std::strcpy(latest_,recording_name_);
        else { char path[128]; std::snprintf(path,sizeof(path),"%s/%s",directory_,recording_name_); filesystem_.remove(path); }
        looper.save_complete(ok); saving_ = nullptr;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_open_stream)
#endif
    std::unique_ptr<StreamFileSource> open_stream(const PlayMidiFilePatch &settings, unsigned route) override {
        const char *filename = settings.filename[0] ? settings.filename : latest_;
        if (!filename[0] || ((recorder.active() || saving_) && !std::strcmp(filename, recording_name_))) return {};
        auto source = filesystem_.file(), index = filesystem_.file();
        if (!source || !index) return {};
        char path[128], index_path[160];
        int source_size = std::snprintf(path, sizeof(path), "%s/%s", directory_, filename);
        int index_size = std::snprintf(index_path, sizeof(index_path), "%s.stream-%u", index_path_, route);
        if (source_size < 0 || unsigned(source_size) >= sizeof(path)
            || index_size < 0 || unsigned(index_size) >= sizeof(index_path)) return {};
        auto stream = std::make_unique<StreamFilePlayer>(filesystem_, std::move(source), std::move(index), index_path);
        if (!stream->start(path, settings)) return {};
        return stream;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_play)
#endif
    bool play(unsigned invocation, const cntrl::midi::PlayMidiFilePatch &settings) {
        if (looper.owned() || storage_busy() || invocation >= cntrl::midi::Bridge::owners || player.active() || player.cleaning()) return false;
        const char *filename = settings.filename[0] ? settings.filename : latest_;
        if (!filename[0] || (recorder.active() && !std::strcmp(filename, recording_name_))) return false;
        char path[128]; std::snprintf(path, sizeof(path), "%s/%s", directory_, filename);
        source_.close(); index_.close();
        source_.open(path, files::Mode::read);
        index_.open(index_path_, files::Mode::truncate);
        if (!source_.opened() || !index_.opened()) return false;
        return player.bind(source_,index_) && player.start(settings, invocation);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_record)
#endif
    bool record(const cntrl::midi::RecordMidiFilePatch &settings, uint64_t start_count, bool start_counter_ready, uint32_t now) {
        if (looper.owned() || storage_busy()) return false;
        if (recorder.active()) {
            bool ok = recorder.finish(now); recording_.close();
            if (ok) std::strcpy(latest_, recording_name_);
            return ok;
        }
        if (!open_recording(start_count,start_counter_ready)) return false;
        for (auto &parser : parsers_) parser.reset();
        return recorder.bind(recording_) && recorder.start(settings, now);
    }
    void receive(unsigned input, const cntrl::midi::Packet &packet, uint32_t now, BeatPosition clock = {}) {
        if (looper.configured()) looper.receive(input,packet.source < 256 ? packet.source % 16 : 0,packet,clock);
        if (looper.owned() || !recorder.active()) return;
        for (unsigned i = 0; i < packet.size(); ++i) receive(input, packet.bytes[i], now, clock);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_receive)
#endif
    void receive(unsigned input, uint8_t byte, uint32_t now, BeatPosition clock = {}) {
        if ((!recorder.active() && !looper.configured()) || input >= parsers_.size()) return;
        cntrl::midi::Packet packet;
        if (parsers_[input].receive(byte, packet)) {
            if (looper.configured()) looper.receive(input,0,packet,clock);
            if (!looper.owned()) recorder.receive(input,packet,now);
        }
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_forget_input)
#endif
    void forget_input(unsigned input) {
        if (input < parsers_.size()) parsers_[input].reset();
        looper.input_connection(input);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_boot)
#endif
    void boot() {
        latest_[0] = 0;
        if (!entries_.open(directory_)) return;
        uint64_t latest_start = 0; unsigned latest_count = 0;
        char name[96] = {};
        bool is_directory = false;
        while (entries_.next(name, sizeof(name), is_directory)) {
            uint64_t start = 0; unsigned count = 0;
            if (!recording_identity(name,start,count) || start < latest_start || (start == latest_start && count <= latest_count)) continue;
            if (is_directory) continue;
            if (completed_file(name)) {
                latest_start = start; latest_count = count; std::strcpy(latest_, name);
            }
        }
        entries_.close();
    }
private:
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_recording_identity)
#endif
    static bool recording_identity(const char *name, uint64_t &start, unsigned &count) {
        const char *end = name + std::strlen(name);
        auto first = std::from_chars(name,end,start);
        if (first.ec != std::errc{} || first.ptr == end || *first.ptr != '-' || !start) return false;
        auto second = std::from_chars(first.ptr + 1,end,count);
        return second.ec == std::errc{} && !std::strcmp(second.ptr,".mid") && count;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_completed_file)
#endif
    bool completed_file(const char *name) {
        char path[128]; std::snprintf(path,sizeof(path),"%s/%s",directory_,name);
        if (!source_.open(path,files::Mode::read)) return false;
        uint8_t header[22], tail[3]; uint64_t size = source_.size();
        bool valid = size >= 26 && source_.read(0,header,sizeof(header))
            && !std::memcmp(header,"MThd",4) && !std::memcmp(header + 14,"MTrk",4)
            && (uint32_t(header[18]) << 24 | uint32_t(header[19]) << 16 | uint32_t(header[20]) << 8 | header[21]) == size - 22
            && source_.read(uint32_t(size - 3),tail,3) && tail[0] == 0xff && tail[1] == 0x2f && !tail[2];
        source_.close(); return valid;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_open_recording)
#endif
    bool open_recording(uint64_t start_count, bool ready) {
        if (!ready || (!filesystem_.exists(directory_) && !filesystem_.make_directory(directory_)) || count_ == UINT32_MAX) return false;
        char number[21]; *std::to_chars(number,number + 20,start_count).ptr = 0;
        std::snprintf(recording_name_,sizeof(recording_name_),"%s-%lu.mid",number,(unsigned long)++count_);
        char path[128]; std::snprintf(path,sizeof(path),"%s/%s",directory_,recording_name_);
        recording_.close(); return recording_.open(path,files::Mode::exclusive);
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_basename)
#endif
    static bool basename(const char *name) {
        if (!*name || !std::strcmp(name,".") || !std::strcmp(name,"..") || std::strlen(name) > 95) return false;
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p)
            if (*p < 32 || *p > 126 || *p == '/' || *p == '\\') return false;
        return true;
    }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(files_browse_loop)
#endif
    void browse_step() {
        char name[96]; bool directory = false;
        if (!entries_.next(name,sizeof(name),directory)) {
            bool ok = !entries_.failed(); entries_.close(); browsing_ = false;
            if (ok) std::strcpy(selected_,browse_best_[0] ? browse_best_ : browse_wrap_);
            return;
        }
        const char *suffix = std::strrchr(name,'.');
        if (directory || name[0] == '.' || !basename(name) || !suffix || (std::strcmp(suffix,".mid") && std::strcmp(suffix,".MID"))) return;
        uint64_t start = 0; unsigned count = 0;
        if (recording_identity(name,start,count) && !completed_file(name)) return;
        auto earlier = [&](const char *a, const char *b) { return browse_reverse_ ? std::strcmp(a,b) > 0 : std::strcmp(a,b) < 0; };
        if (!browse_wrap_[0] || earlier(name,browse_wrap_)) std::strcpy(browse_wrap_,name);
        if ((!selected_[0] || earlier(selected_,name)) && (!browse_best_[0] || earlier(name,browse_best_))) std::strcpy(browse_best_,name);
    }
    MidiFile *saving_ = nullptr;
    uint32_t save_offset_ = 0;
    unsigned save_tempo_ = 500000, save_stage_ = 0;
    bool loading_ = false, browsing_ = false, browse_reverse_ = false;
    char selected_[96] = {}, browse_best_[96] = {}, browse_wrap_[96] = {};
    files::FileSystem &filesystem_;
    files::Directory &entries_;
    std::vector<cntrl::midi::Parser> parsers_;
    char latest_[96] = {}, recording_name_[96] = {};
    uint32_t count_ = 0;
    const char *directory_;
    const char *index_path_;
};

} // namespace cntrl::midi
