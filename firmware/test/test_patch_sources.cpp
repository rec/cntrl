#include "patch_hardware.h"
#include <cntrl/patch_sources.h>
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware {
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    unsigned switch_capacity() const override { return 5; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
};
struct File : cntrl::SetupBundleFile {
    std::vector<uint8_t> bytes;
    unsigned reads = 0;
    uint32_t size() const override { return unsigned(bytes.size()); }
    bool read(uint32_t at, uint8_t *data, unsigned n) override {
        ++reads; if (at > size() || n > size() - at) return false;
        std::memcpy(data, bytes.data() + at, n); return true;
    }
};
int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream input(argv[1]); std::string hex; input >> hex;
    std::vector<uint8_t> setup;
    for (unsigned i = 0; i < hex.size(); i += 2) setup.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    unsigned length = unsigned(setup.size()), table_start = 40 + 3 * 76, payload = table_start + 10;
    File file; file.bytes.resize(payload + 3 * length);
    auto *p = file.bytes.data(); std::memcpy(p, "CSTB", 4); p[4] = 1; p[6] = 3;
    cntrl::put32(p + 8, file.size()); cntrl::put32(p + 16, 524288 - 262144 - 741 - 2 * length);
    cntrl::put32(p + 20, 262144); cntrl::put32(p + 28, 262144 + 741);
    cntrl::put32(p + 32, 3 * length); cntrl::put32(p + 36, 2 * length);
    p[table_start] = 3; cntrl::put32(p + table_start + 2, length); cntrl::put32(p + table_start + 6, 2 * length);
    for (unsigned i = 0; i < 3; ++i) {
        p[40 + i * 76] = uint8_t('a' + i); cntrl::put32(p + 40 + i * 76 + 64, length);
        p[40 + i * 76 + 72] = i < 2;
        std::memcpy(p + payload + i * length, setup.data(), length);
    }
    cntrl::put32(p + 12, cntrl::checksum(p + 40, file.size() - 40));
    Board board; cntrl::SetupTable table; assert(table.open(file, board));
    cntrl::StoredPatchSources sources(table); sources.begin(); sources.begin();
    const cntrl::Document *a = nullptr, *b = nullptr; uint32_t revision;
    assert(sources.acquire(0, a, revision) && sources.acquire(1, b, revision));
    unsigned reads = file.reads; const cntrl::Document *same = nullptr;
    assert(sources.acquire(0, same, revision) && same == a && file.reads == reads);
    assert(!table.get(2)); sources.end(); assert(!table.get(2));
    sources.end(); assert(table.get(2));
    sources.begin(); assert(sources.acquire(2, same, revision)); sources.end();
    assert(table.get(2) == same); // Ending an invocation keeps the cached view.
    assert(!sources.acquire(3, same, revision));
}
