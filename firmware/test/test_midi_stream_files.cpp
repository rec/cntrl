#include <cntrl/midi_files.h>
#include <cntrl/midi_processing.h>
#include <cassert>
#include <map>
#include <string>

using Bytes = std::vector<uint8_t>;
struct Storage;
struct File : cntrl::files::File {
    Storage &storage;
    Bytes *data = nullptr;
    bool writable = true, readable = true, syncable = true;
    unsigned writes = 0, reads = 0, maximum_write = 0;
    explicit File(Storage &owner) : storage(owner) {}
    bool open(const char *path, cntrl::files::Mode mode) override;
    void close() override { data = nullptr; }
    bool opened() const override { return data != nullptr; }
    uint32_t size() const override { return data ? unsigned(data->size()) : 0; }
    bool read(uint32_t offset, uint8_t *out, unsigned length) override {
        ++reads;
        if (!readable) return false;
        if (!data || offset > data->size() || length > data->size() - offset) return false;
        std::memcpy(out, data->data() + offset, length); return true;
    }
    bool write(uint32_t offset, const uint8_t *in, unsigned length) override {
        ++writes; maximum_write = std::max(maximum_write,length);
        if (!data || !writable) return false;
        if (offset + length > data->size()) data->resize(offset + length);
        std::memcpy(data->data() + offset, in, length); return true;
    }
    bool sync() override { return data != nullptr && syncable; }
};
struct Directory : cntrl::files::Directory {
    Storage *storage;
    std::string prefix, cursor;
    explicit Directory(Storage *owner = nullptr) : storage(owner) {}
    bool open(const char *path) override { prefix = std::string(path) + "/"; cursor.clear(); return storage != nullptr; }
    bool next(char *, unsigned, bool &) override;
    void close() override { prefix.clear(); }
};
struct Storage : cntrl::files::FileSystem {
    std::map<std::string, Bytes> files;
    bool exists(const char *path) override { return files.count(path); }
    bool make_directory(const char *) override { return true; }
    bool rename(const char *from, const char *to) override {
        if (!exists(from)) return false;
        files[to] = std::move(files[from]); files.erase(from); return true;
    }
    bool remove(const char *path) override { return files.erase(path) != 0; }
    std::unique_ptr<cntrl::files::File> file() override { return std::make_unique<File>(*this); }
};
bool File::open(const char *path, cntrl::files::Mode mode) {
    close();
    if (mode == cntrl::files::Mode::read && !storage.exists(path)) return false;
    if (mode == cntrl::files::Mode::exclusive && storage.exists(path)) return false;
    data = &storage.files[path];
    if (mode == cntrl::files::Mode::truncate) data->clear();
    return true;
}
bool Directory::next(char *name, unsigned capacity, bool &directory) {
    if (!storage || prefix.empty()) return false;
    auto i = cursor.empty() ? storage->files.lower_bound(prefix) : storage->files.upper_bound(cursor);
    if (i == storage->files.end() || i->first.compare(0,prefix.size(),prefix)) return false;
    cursor = i->first; std::string value = cursor.substr(prefix.size());
    directory = value.find('/') != std::string::npos;
    std::snprintf(name,capacity,"%s",value.c_str()); return true;
}
struct Sink : cntrl::midi::Sink {
    Bytes bytes;
    bool write_packet(unsigned route, const cntrl::midi::Packet &packet) override {
        assert(route == 0);
        bytes.insert(bytes.end(), packet.bytes, packet.bytes + packet.size()); return true;
    }
};
void independent_file_generators_do_not_share_position_or_indexes() {
    Storage storage;
    storage.files["/midi/a.mid"] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,
        'M','T','r','k',0,0,0,13,0,0x90,60,97,0x83,0x60,0x80,60,0,0,0xff,0x2f,0};
    File source(storage), index(storage), recording(storage); Directory directory;
    cntrl::midi::MidiFiles files(storage, source, index, recording, directory, 1, "/midi", "/index");
    cntrl::midi::PlayMidiFilePatch settings;
    std::strcpy(settings.filename, "a.mid");
    auto first = files.open_stream(settings, 0), second = files.open_stream(settings, 1);
    assert(first && second && storage.exists("/index.stream-0") && storage.exists("/index.stream-1"));
    Sink first_sink, second_sink;
    for (unsigned i = 0; i < 32; ++i) first->poll(0, first_sink);
    assert((first_sink.bytes == Bytes{0x90,60,97}));
    assert(second_sink.bytes.empty());
    for (unsigned i = 0; i < 32; ++i) second->poll(1000000, second_sink);
    assert(first_sink.bytes == second_sink.bytes);
    for (unsigned i = 0; i < 32; ++i) first->poll(500000, first_sink);
    assert(first->finished() && !first->failed());
    assert(!second->finished());
    first.reset();
    assert(!storage.exists("/index.stream-0") && storage.exists("/index.stream-1"));
    std::strcpy(settings.filename, "missing.mid");
    assert(!files.open_stream(settings, 2));
    assert(!storage.exists("/index.stream-2"));
}
void file_generator_runs_through_finite_reverse() {
    Storage storage;
    storage.files["/midi/a.mid"] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,
        'M','T','r','k',0,0,0,13,0,0x90,60,97,0x83,0x60,0x80,60,0,0,0xff,0x2f,0};
    File source(storage), index(storage), recording(storage); Directory directory;
    cntrl::midi::MidiFiles files(storage, source, index, recording, directory, 1, "/midi", "/index");
    Bytes section{'C','S','T','R',0,0,0,0,1,0,1,0,0xff,0xff,0,0,1,0,2,0};
    section.push_back('a'); section.insert(section.end(), 63, 0);
    section.insert(section.end(), {9,0,126,0});
    const char filename[] = "a.mid";
    section.insert(section.end(), filename, filename + 5); section.insert(section.end(), 91, 0);
    section.insert(section.end(), {1,0,0,0});
    section.insert(section.end(), 16, 0);
    double speed = 1; const auto *bytes = reinterpret_cast<const uint8_t *>(&speed);
    section.insert(section.end(), bytes, bytes + sizeof(speed));
    section.insert(section.end(), {0,0,7,0,8,0}); section.insert(section.end(), 8, 0);
    for (unsigned i = 0; i < 4; ++i) section[4 + i] = uint8_t(section.size() >> (i * 8));
    const cntrl::midi::OutputConnection endpoint{"din", "DIN", 0, cntrl::midi::OutputTransport::physical, false, true};
    cntrl::midi::Streams streams; Sink sink;
    streams.file_provider(&files);
    assert(streams.load(section.data(), unsigned(section.size()), {&endpoint,1}, 0));
    for (unsigned i = 0; i < 32; ++i) streams.service(sink, 0);
    assert(sink.bytes.empty());
    for (unsigned i = 0; i < 32; ++i) streams.service(sink, 500000);
    assert((sink.bytes == Bytes{0x90,60,97}));
    for (unsigned i = 0; i < 32; ++i) streams.service(sink, 1000000);
    assert((sink.bytes == Bytes{0x90,60,97,0x80,60,0}));
    assert(!streams.voices());
}
void loop_save_is_exclusive_bounded_and_load_never_autoplays() {
    Storage storage;
    Bytes existing{'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k',0,0,0,8,0,0xc0,12,0x83,0x60,0xff,0x2f,0};
    storage.files["/midi/42-1.mid"] = existing;
    File source(storage), index(storage), recording(storage); Directory directory(&storage);
    cntrl::midi::MidiFiles files(storage,source,index,recording,directory,1,"/midi","/index"); Sink sink;
    assert(files.looper.configure({},sink));
    constexpr uint64_t beat = cntrl::midi::MidiLooper::beat;
    assert(files.looper.record({0,1,true},500000)); files.looper.poll({beat,1,true},true,1,sink);
    files.looper.receive(0,0,{9,{0x90,60,99}},{beat,1,true});
    files.looper.receive(0,0,{8,{0x80,60,0}},{beat + beat / 2,1,true});
    assert(files.looper.record({beat + beat / 2,1,true},500000)); files.looper.poll({beat * 2,1,true},true,1,sink);
    assert(recording.writes == 0 && source.reads == 0 && index.writes == 0);
    assert(!files.play(0,{}) && !files.record({},42,true,0));
    assert(!files.save_loop(750000,42,true));
    assert(storage.files["/midi/42-1.mid"] == existing);
    assert(files.save_loop(750000,42,true));
    assert(!files.looper.record({beat * 2,1,true},500000));
    for (unsigned i = 0; i < 20 && files.storage_busy(); ++i) {
        files.looper.poll({beat * 2 + beat / 2,1,true},true,1,sink);
        unsigned before = recording.writes; files.loop_storage_poll(); assert(recording.writes - before <= 1);
    }
    assert(!files.storage_busy() && !files.looper.unsaved() && files.looper.playing());
    const auto saved = storage.files["/midi/42-2.mid"];
    assert(saved[26] == 11 && saved[27] == 113 && saved[28] == 176 && recording.maximum_write <= 512);
    recording.syncable = false;
    assert(files.save_loop(500000,42,true));
    for (unsigned i = 0; i < 20 && files.storage_busy(); ++i) files.loop_storage_poll();
    assert(!storage.exists("/midi/42-3.mid") && storage.files["/midi/42-2.mid"] == saved);
    recording.syncable = true;
    files.looper.stop(); files.looper.poll({beat * 2 + beat / 2,1,true},true,1,sink);
    unsigned sent = unsigned(sink.bytes.size());
    assert(files.load_loop("42-2.mid"));
    for (unsigned i = 0; i < 64 && files.storage_busy(); ++i) {
        files.looper.poll({0,1,false},true,1,sink); files.loop_storage_poll();
    }
    assert(!files.storage_busy() && files.looper.state() == cntrl::midi::MidiLooper::State::stopped);
    assert(files.looper.length() == beat && sink.bytes.size() == sent);
    storage.files["/midi/42-4.mid"] = Bytes(saved.begin(),saved.begin() + 22);
    assert(files.browse_loop(false,true));
    for (unsigned i = 0; i < 20 && files.browsing(); ++i) files.loop_storage_poll();
    assert(!std::strcmp(files.loop_filename(),"42-1.mid"));
    assert(files.browse_loop(false));
    for (unsigned i = 0; i < 20 && files.browsing(); ++i) files.loop_storage_poll();
    assert(!std::strcmp(files.loop_filename(),"42-2.mid"));
    assert(files.browse_loop(false));
    for (unsigned i = 0; i < 20 && files.browsing(); ++i) files.loop_storage_poll();
    assert(!std::strcmp(files.loop_filename(),"42-1.mid"));
}
void loop_capture_keeps_running_status_and_usb_cables_separate() {
    Storage storage; File source(storage), index(storage), recording(storage); Directory directory;
    cntrl::midi::MidiFiles files(storage,source,index,recording,directory,1,"/midi","/index"); Sink sink;
    assert(files.looper.configure({},sink));
    constexpr uint64_t beat = cntrl::midi::MidiLooper::beat;
    for (uint8_t byte : {uint8_t(0x90),uint8_t(60),uint8_t(100)}) files.receive(0,byte,0,{0,1,true});
    assert(files.looper.record({0,1,true},500000)); files.looper.poll({beat,1,true},true,1,sink);
    files.receive(0,uint8_t(62),1,{beat,1,true}); files.receive(0,uint8_t(101),1,{beat,1,true});
    files.receive(0,uint8_t(60),2,{beat + beat / 4,1,true}); files.receive(0,uint8_t(0),2,{beat + beat / 4,1,true});
    files.receive(0,uint8_t(62),3,{beat + beat / 2,1,true}); files.receive(0,uint8_t(0),3,{beat + beat / 2,1,true});
    assert(files.looper.record({beat + beat / 2,1,true},500000)); files.looper.poll({beat * 2,1,true},true,1,sink);
    assert((sink.bytes == Bytes{0x90,62,101}) && files.looper.events() == 2);
    assert(files.looper.clear()); files.looper.poll({beat * 2,1,true},true,1,sink);
    sink.bytes.clear();
    assert(files.looper.record({beat * 2,1,true},500000)); files.looper.poll({beat * 3,1,true},true,1,sink);
    files.receive(0,cntrl::midi::Packet{9,{0x90,60,99},0},0,{beat * 3,1,true});
    files.receive(0,cntrl::midi::Packet{8,{0x80,60,0},1},0,{beat * 3 + beat / 4,1,true});
    files.receive(0,cntrl::midi::Packet{8,{0x80,60,0},0},0,{beat * 3 + beat / 2,1,true});
    assert(files.looper.record({beat * 3 + beat / 2,1,true},500000)); files.looper.poll({beat * 4,1,true},true,1,sink);
    assert(files.looper.events() == 2);
    files.looper.poll({beat * 4 + beat / 4,1,true},true,1,sink);
    assert(sink.bytes.size() == 3);
    files.looper.poll({beat * 4 + beat / 2,1,true},true,1,sink);
    assert(sink.bytes.size() == 6);
}
int main() {
    cntrl::memory_budget = 524288;
    independent_file_generators_do_not_share_position_or_indexes();
    file_generator_runs_through_finite_reverse();
    loop_save_is_exclusive_bounded_and_load_never_autoplays();
    loop_capture_keeps_running_status_and_usb_cables_separate();
    assert(cntrl::memory_used == 0);
}
