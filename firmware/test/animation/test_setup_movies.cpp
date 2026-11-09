#include "../patch_hardware.h"
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cassert>

struct Hardware : TestPatchHardware {
    std::vector<uint32_t> startup, idle;
    bool enabled = false;
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool value, uint32_t) override { enabled = value; }
    void error(const char *) override { assert(false); }
    void set_movies(const uint8_t *a, unsigned na, const uint8_t *b, unsigned nb) override {
        for (unsigned i = 0; i < na; ++i) startup.push_back(cntrl::u32(a + i * 4));
        for (unsigned i = 0; i < nb; ++i) idle.push_back(cntrl::u32(b + i * 4));
    }
};

int main(int argc, char **argv) {
    assert(argc == 3);
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> data{std::istreambuf_iterator<char>(input), {}};
    uint32_t key = uint32_t(std::stoul(argv[2], nullptr, 16));
    Hardware hardware; cntrl::Document document;
    assert(document.load(data.data(), unsigned(data.size()), hardware));
    TestController controller(hardware); controller.activate(document);
    assert(hardware.enabled && hardware.startup == std::vector<uint32_t>{key}
        && hardware.idle == std::vector<uint32_t>{key});
    unsigned offset = unsigned(document.movie_section() - document.data());
    data[offset + 4] = 255; // The count cannot run beyond the setup's key table.
    cntrl::put32(data.data() + 20, cntrl::checksum(data.data() + 28, unsigned(data.size()) - 28));
    cntrl::Document invalid;
    assert(!invalid.load(data.data(), unsigned(data.size()), hardware));
}
