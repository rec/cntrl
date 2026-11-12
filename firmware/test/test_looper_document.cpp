#include "patch_hardware.h"
#include <cntrl/midi_looper.h>
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
    cntrl::memory_budget = 524288;
    assert(argc == 2);
    std::ifstream file(argv[1]); std::string hex; file >> hex;
    std::vector<uint8_t> data;
    for (unsigned i = 0; i < hex.size(); i += 2) data.push_back(uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));
    Hardware hardware; cntrl::Document document;
    assert(document.load(data.data(),unsigned(data.size()),hardware));
    const auto *settings = document.looper_settings(); assert(settings);
    unsigned meta = 76 + cntrl::u16(data.data() + 12) * 68 + cntrl::u16(data.data() + 14) * 68 + cntrl::u16(data.data() + 16) * 82;
    assert(document.page_ram2() == cntrl::u32(data.data() + meta + cntrl::scrolling_offset + 6));
    assert(cntrl::u16(settings + 12) == 512 && cntrl::u16(settings + 6) == 3);
    {
        cntrl::midi::LoopFile source,index;
        cntrl::midi::MidiFilePlayer player(source,index);
        cntrl::midi::MidiFileRecorder recorder(source);
        cntrl::midi::MidiLooper loop(player,recorder);
        struct Sink : cntrl::midi::Sink {
            bool write_packet(unsigned,const cntrl::midi::Packet &) override { assert(false); return false; }
        } sink;
        const unsigned used = cntrl::memory_used;
        assert(loop.configure({cntrl::u16(settings + 8),cntrl::u16(settings + 6),cntrl::u16(settings + 10),cntrl::u16(settings + 12)},sink));
        assert(cntrl::memory_used == used);
        cntrl::memory_fail_after = 0;
        assert(!loop.record({0,1,true},500000));
        assert(loop.state() == cntrl::midi::MidiLooper::State::empty && cntrl::memory_used == used);
        cntrl::memory_fail_after = -1;
    }
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
    cntrl::put32(data.data() + meta + cntrl::scrolling_offset + 6,hardware.memory_capacity() - hardware.memory_runtime() + 1);
    cntrl::put32(data.data() + 20,cntrl::checksum(data.data() + 28,unsigned(data.size()) - 28));
    assert(!document.load(data.data(),unsigned(data.size()),hardware));
}
