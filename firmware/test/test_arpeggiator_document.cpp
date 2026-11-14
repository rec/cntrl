#include "patch_hardware.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct Hardware : TestPatchHardware {
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned,unsigned) const override { return true; }
    unsigned memory_capacity() const override { return 1024 * 1024; }
    unsigned memory_runtime() const override { return 0; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool send(const char *,const uint8_t *,unsigned) override { return false; }
    void show(unsigned,const cntrl::Pane &) override {}
    void set_brightness(uint8_t,bool = false) override {}
    void set_splash(bool,uint32_t) override {}
    void error(const char *) override { assert(false); }
};

int main(int argc,char **argv) {
    assert(argc == 2 || argc == 3);
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> data{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    Hardware hardware; cntrl::Document document;
    assert(document.load(data.data(),unsigned(data.size()),hardware));
    const auto *settings = document.arpeggiator_settings();
    assert(settings && !std::memcmp(settings,"CARP",4));
    if (argc == 3) {
        std::ifstream profile(argv[2],std::ios::binary);
        std::vector<uint8_t> expected{std::istreambuf_iterator<char>(profile),std::istreambuf_iterator<char>()};
        assert(expected.size() == 50 && !std::memcmp(settings,expected.data(),expected.size()));
    }
    assert(document.page_layout().page(0)[0] == cntrl::PageLayout::arpeggiator);
    unsigned offset = unsigned(settings - data.data());
    for (unsigned field : {4u,5u,6u,7u}) {
        auto invalid = data; invalid[offset + field] = 255;
        cntrl::put32(invalid.data() + 20,cntrl::checksum(invalid.data() + 28,unsigned(invalid.size()) - 28));
        assert(!document.load(invalid.data(),unsigned(invalid.size()),hardware));
    }
}
