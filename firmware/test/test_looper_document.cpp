#include "patch_hardware.h"
#include <cassert>
#include <fstream>
#include <string>
#include <vector>

struct Hardware : TestPatchHardware {
    std::vector<unsigned> actions;
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned,unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool send(const char *,const uint8_t *,unsigned) override { assert(false); return false; }
    void show(unsigned,const cntrl::Pane &) override {}
    void set_brightness(uint8_t,bool = false) override {}
    void set_splash(bool,uint32_t) override {}
    void error(const char *) override { assert(false); }
    bool looper_action(unsigned action,const char *filename) override {
        actions.push_back(action);
        assert(action == 5 ? !std::strcmp(filename,"42-1.mid") : !*filename);
        return true;
    }
};

int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream file(argv[1]); std::string hex; file >> hex;
    std::vector<uint8_t> data;
    for (unsigned i = 0; i < hex.size(); i += 2) data.push_back(uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));
    Hardware hardware; cntrl::Document document;
    assert(document.load(data.data(),unsigned(data.size()),hardware));
    const auto *settings = document.looper_settings(); assert(settings && document.page_ram2() == 113344);
    assert(cntrl::u16(settings + 12) == 512 && cntrl::u16(settings + 6) == 3);
    cntrl::PatchExecutor executor(hardware,hardware,hardware,0);
    for (unsigned i = 0; i < document.patches(); ++i) {
        assert(executor.start(document,document.browser_root(i)));
        for (unsigned work = 0; work < 64 && executor.busy(); ++work) executor.tick(0);
        assert(executor.status() == cntrl::PatchExecutor::Status::completed);
        assert(hardware.actions.back() == i);
    }
    unsigned config = unsigned(settings - data.data());
    auto valid = data;
    data[config + 4] = 2;
    cntrl::put32(data.data() + 20,cntrl::checksum(data.data() + 28,unsigned(data.size()) - 28));
    assert(!document.load(data.data(),unsigned(data.size()),hardware));
    data = valid;
    unsigned meta = 76 + cntrl::u16(data.data() + 12) * 68 + cntrl::u16(data.data() + 14) * 68 + cntrl::u16(data.data() + 16) * 82;
    cntrl::put32(data.data() + meta + cntrl::scrolling_offset + 6,113343);
    cntrl::put32(data.data() + 20,cntrl::checksum(data.data() + 28,unsigned(data.size()) - 28));
    assert(!document.load(data.data(),unsigned(data.size()),hardware));
}
