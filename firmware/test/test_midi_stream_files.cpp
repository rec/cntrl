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
    explicit File(Storage &owner) : storage(owner) {}
    bool open(const char *path, cntrl::files::Mode mode) override;
    void close() override { data = nullptr; }
    bool opened() const override { return data != nullptr; }
    uint32_t size() const override { return data ? unsigned(data->size()) : 0; }
    bool read(uint32_t offset, uint8_t *out, unsigned length) override {
        if (!data || offset > data->size() || length > data->size() - offset) return false;
        std::memcpy(out, data->data() + offset, length); return true;
    }
    bool write(uint32_t offset, const uint8_t *in, unsigned length) override {
        if (!data) return false;
        if (offset + length > data->size()) data->resize(offset + length);
        std::memcpy(data->data() + offset, in, length); return true;
    }
    bool sync() override { return data != nullptr; }
};
struct Directory : cntrl::files::Directory {
    bool open(const char *) override { return false; }
    bool next(char *, unsigned, bool &) override { return false; }
    void close() override {}
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
int main() {
    independent_file_generators_do_not_share_position_or_indexes();
    file_generator_runs_through_finite_reverse();
}
