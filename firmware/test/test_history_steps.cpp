#include "storage.h"
#include <cassert>

struct BoundedStorage : TestStorage {
    unsigned file_calls = 0, entries = 0;
    bool read(unsigned s, unsigned offset, uint8_t *data, unsigned n) override {
        assert(n <= 512); ++file_calls; return TestStorage::read(s, offset, data, n);
    }
    bool write(const uint8_t *data, unsigned n) override {
        assert(n <= 512); ++file_calls; return TestStorage::write(data, n);
    }
    bool revision_scan_step(uint64_t &best) override {
        ++entries; return TestStorage::revision_scan_step(best);
    }
};

int main() {
    uint64_t number; char text[21];
    assert(cntrl::parse_revision("18446744073709551615", number) && number == UINT64_MAX);
    cntrl::revision_text(number, text); assert(!std::strcmp(text, "18446744073709551615"));
    assert(!cntrl::parse_revision("18446744073709551616", number));
    assert(!cntrl::parse_revision("-1", number));
    assert(!cntrl::parse_revision("", number));
    cntrl::revision_text(0, text); assert(!std::strcmp(text, "0"));
    BoundedStorage storage;
    cntrl::Revisions revisions(storage);
    std::vector<uint8_t> source(cntrl::max_document), downloaded(cntrl::max_document);
    for (unsigned i = 0; i < source.size(); ++i) source[i] = uint8_t(i);
    // Include many reserved/incomplete entries. Scanning must not process the
    // directory in a single tick, and the next revision must skip every number.
    for (uint64_t i = 1; i <= 2000; ++i) storage.choose_revision("Test", i);
    unsigned opportunities = 0;
    auto run = [&] {
        unsigned ticks = 0;
        while (revisions.busy()) {
            storage.file_calls = storage.entries = 0;
            revisions.tick();
            assert(storage.file_calls <= 1 && storage.entries <= 1);
            // The caller regains control for MIDI/UI service after every step.
            ++opportunities; ++ticks; assert(ticks < 3000000);
        }
        return ticks;
    };
    revisions.append("Test", "Large snapshot", 123, source.data(), unsigned(source.size()));
    assert(run() > 2000 && revisions.success());
    assert(revisions.info().number == 2001);
    revisions.read("Test", 2001, downloaded.data());
    assert(run() > 128 && revisions.success() && downloaded == source);
    assert(!std::strcmp(revisions.info().label, "Large snapshot"));
    storage.remaining = 100;
    revisions.append("Test", "Interrupted", 123, source.data(), unsigned(source.size()));
    run(); assert(!revisions.success()); storage.remaining = -1;
    revisions.next("Test", UINT64_MAX, downloaded.data());
    assert(run() > 2000 && revisions.success() && revisions.info().number == 2001);
    revisions.erase("Test", 0);
    assert(run() > 2000 && revisions.success());
    revisions.next("Test", UINT64_MAX, downloaded.data());
    run(); assert(!revisions.success());
    assert(opportunities > 10000);
}
