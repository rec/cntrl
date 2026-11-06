#include "patch_switcher/startup_counter.h"
#include <algorithm>
#include <cassert>
#include <vector>

struct File {
    std::vector<char> data;
    unsigned position = 0, writes = 0, limit = StartupCounter::record_size;
    bool open = true, writable = true, synced = true;
    explicit operator bool() const { return open; }
    unsigned size() const { return unsigned(data.size()); }
    bool seekSet(unsigned offset) { position = offset; return open; }
    int read(char *bytes, unsigned length) {
        unsigned n = position < data.size() ? std::min(length, unsigned(data.size()) - position) : 0;
        if (n) std::memcpy(bytes, data.data() + position, n);
        position += n; return int(n);
    }
    unsigned write(const char *bytes, unsigned length) {
        ++writes; if (!writable) return 0;
        unsigned n = std::min(length, limit);
        if (position + n > data.size()) data.resize(position + n);
        std::memcpy(data.data() + position, bytes, n); position += n; return n;
    }
    bool sync() const { return synced; }
};

int main() {
    File file;
    for (unsigned i = 1; i <= 10; ++i) {
        StartupCounter counter; counter.start(file);
        assert(counter.status == StartupCounter::Status::ready && counter.count == i);
    }
    assert(file.size() == 1024 && file.writes == 10);
    // Corrupt the newest copy. The previous copy remains a usable baseline.
    file.data[StartupCounter::record_size + 1] = '!';
    StartupCounter recovered; recovered.start(file);
    assert(recovered.status == StartupCounter::Status::ready && recovered.count == 10);
    // A partial update preserves the previous sector and is detected next time.
    File torn; StartupCounter first; first.start(torn);
    torn.limit = 20; StartupCounter interrupted; interrupted.start(torn);
    assert(interrupted.status == StartupCounter::Status::error && interrupted.count == 1);
    torn.limit = StartupCounter::record_size;
    StartupCounter restart; restart.start(torn);
    assert(restart.status == StartupCounter::Status::ready && restart.count == 2);
    torn.writable = false; StartupCounter readonly; readonly.start(torn);
    assert(readonly.status == StartupCounter::Status::error && readonly.count == 2);
    // An unrecognizable existing file must not silently reset the count.
    File corrupt; corrupt.data.assign(1024, '?'); StartupCounter invalid; invalid.start(corrupt);
    assert(invalid.status == StartupCounter::Status::error && !corrupt.writes);
    File missing; missing.open = false; StartupCounter unavailable;
    assert(unavailable.status == StartupCounter::Status::unavailable);
    unavailable.start(missing); assert(unavailable.status == StartupCounter::Status::error);
    File failed_sync; failed_sync.synced = false; StartupCounter unsaved; unsaved.start(failed_sync);
    assert(unsaved.status == StartupCounter::Status::error);
    File full; full.data.assign(StartupCounter::record_size, ' ');
    int length = std::snprintf(full.data.data(), full.data.size(), "[slot0]\nstarts = %llu\ncheck = \"%016llx\"\n",
        (unsigned long long)INT64_MAX, (unsigned long long)~uint64_t(INT64_MAX));
    full.data[unsigned(length)] = ' '; full.data.back() = '\n';
    StartupCounter limit; limit.start(full);
    assert(limit.status == StartupCounter::Status::error && limit.count == INT64_MAX && !full.writes);
}
