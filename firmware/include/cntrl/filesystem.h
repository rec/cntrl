#pragma once
#include <cntrl/midi_file.h>
#include <memory>

namespace cntrl::files {
// Counted I/O returns -1 for I/O failure or position_error for offset positioning failure.
constexpr int position_error = -2;
enum class Mode { read, create, exclusive, truncate };
struct File : midi::MidiFile {
    virtual bool open(const char *path, Mode mode) = 0;
    virtual void close() = 0;
    virtual bool opened() const = 0;
    virtual int read_some(uint32_t offset, uint8_t *data, unsigned length) {
        return read(offset, data, length) ? int(length) : -1;
    }
    virtual int write_some(uint32_t offset, const uint8_t *data, unsigned length) {
        return write(offset, data, length) ? int(length) : 0;
    }
    virtual bool resize(uint32_t) { return false; }
};
struct Directory {
    virtual ~Directory() = default;
    virtual bool open(const char *path) = 0;
    virtual bool next(char *name, unsigned capacity, bool &is_directory) = 0;
    virtual void close() = 0;
    virtual uint64_t entry_size() const { return 0; }
    virtual bool failed() const { return false; }
};
struct FileSystem {
    virtual ~FileSystem() = default;
    virtual bool exists(const char *path) = 0;
    virtual bool make_directory(const char *path) = 0;
    virtual bool rename(const char *from, const char *to) = 0;
    virtual bool remove(const char *) { return false; }
    virtual bool available() const { return true; }
    virtual std::unique_ptr<Directory> directory() { return {}; }
};
} // namespace cntrl::files
