#include <cntrl/setup_files.h>
#include "filesystem.h"
#include <array>
#include <cassert>

int main(int argc, char **argv) {
    assert(argc == 2);
    TestFileSystem filesystem(argv[1]);
    TestFile reader(filesystem), writer(filesystem);
    TestDirectory history(filesystem);
    cntrl::SetupFiles storage(filesystem, reader, writer, history);
    assert(!storage.available()); storage.ready = true;
    assert(storage.available());
    storage.choose("example");
    const uint8_t bytes[] = {1, 2, 3, 4};
    assert(storage.begin(1)); assert(storage.write(bytes, sizeof(bytes)));
    assert(storage.flush());
    const uint8_t replacement = 9;
    assert(storage.patch(1, &replacement, 1));
    assert(storage.write(bytes, sizeof(bytes))); // Patch does not alter append position.
    assert(storage.finish()); assert(storage.size(1) == 8);
    uint8_t copied[8]; assert(storage.read(1, 0, copied, sizeof(copied)));
    assert(copied[0] == 1 && copied[1] == 9 && copied[4] == 1);
    assert(!storage.write(bytes, sizeof(bytes)));
    std::array<uint8_t, 208> record{};
    for (unsigned revision : {1, 2, 4}) {
        storage.choose_revision("example", revision);
        assert(storage.begin(0)); assert(storage.write(record.data(), record.size())); assert(storage.finish());
        assert(!storage.begin(0)); // Existing history is immutable.
    }
    uint64_t best = 0;
    storage.revision_scan("example", 4, true);
    while (!storage.revision_scan_step(best)) {}
    assert(best == 2);
    assert(storage.remove_revision("example", 2));
    assert(filesystem.exists("/cntrl/history/example/0000000000000002"));
    best = 0; storage.revision_scan("example", 4, true);
    while (!storage.revision_scan_step(best)) {}
    assert(best == 1);
    best = 0; storage.revision_scan("example", 4, false);
    while (!storage.revision_scan_step(best)) {}
    assert(best == 2); // Deleted revision numbers remain reserved.
}
