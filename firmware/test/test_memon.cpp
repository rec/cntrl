#include <cntrl/ui/memon.h>
#include "patch_hardware.h"
#include <cassert>

using cntrl::MemonSettings;
using cntrl::ui::Memon;

struct MemonHardware : TestPatchHardware {
    std::vector<std::vector<uint8_t>> packets;
    bool blocked = false;
    bool send(const char *, const uint8_t *bytes, unsigned size) override {
        if (blocked) return false;
        packets.emplace_back(bytes, bytes + size);
        return true;
    }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
};

int main() {
    using Bytes = std::vector<uint8_t>;
    using Packets = std::vector<Bytes>;
    MemonHardware hardware;
    Memon game;
    MemonSettings settings;
    settings.flags |= MemonSettings::repeat;
    game.configure(settings);
    game.start(0, 1, hardware);
    const unsigned first = 1; // Seed 1 starts with the second tile.
    assert(game.lit() == (1u << first));
    assert(hardware.packets.back() == Bytes({0x90, 64, 100, 0xb0, 2, 100}));
    game.tick(250, hardware);
    assert(hardware.packets.back() == Bytes({0x80, 64, 0, 0xb0, 2, 0}));
    assert(game.remaining_ms(250) == 5000 && game.round() == 1 && game.position() == 1);
    game.press(first, 5100, hardware);
    game.press(first, 5300, hardware);
    assert(game.phase() == Memon::Phase::replay_wait && game.score() == 0);
    game.tick(6299, hardware);
    assert(!game.lit());
    game.tick(6300, hardware);
    assert(game.lit() == (1u << first));
    game.tick(6550, hardware);
    assert(game.remaining_ms(6550) == 5000);
    game.press(first, 11500, hardware);
    game.tick(11750, hardware);
    assert(game.score() == 1 && game.phase() == Memon::Phase::next_round);
    game.tick(12250, hardware);
    game.tick(12500, hardware);
    game.tick(12750, hardware);
    game.tick(13000, hardware);
    game.tick(18000, hardware);
    assert(game.result() == Memon::Result::timeout && game.score() == 1 && game.lit() == 15);
    assert(hardware.packets.back() == Bytes({0x90, 60, 100, 0xb0, 2, 100}));
    hardware.packets.clear();
    game.tick(18499, hardware);
    assert(hardware.packets.empty());
    game.tick(18500, hardware);
    game.tick(19000, hardware);
    game.tick(19500, hardware);
    assert(hardware.packets == Packets({{0x90, 64, 100}, {0x90, 67, 100}, {0x90, 70, 100}}));
    hardware.packets.clear();
    game.tick(20000, hardware);
    assert(hardware.packets == Packets({{0x80, 60, 0}, {0x80, 64, 0}, {0x80, 67, 0}, {0x80, 70, 0, 0xb0, 2, 0}}));

    game.start(21000, 1, hardware);
    game.tick(21250, hardware);
    game.press(0, 21300, hardware);
    game.tick(21550, hardware);
    game.tick(22050, hardware);
    hardware.blocked = true;
    game.leave(22070, hardware);
    hardware.blocked = false;
    hardware.packets.clear();
    game.tick(22090, hardware);
    assert(hardware.packets == Packets({{0x80, 60, 0}, {0x80, 64, 0, 0xb0, 2, 0}}));

    settings.flags = MemonSettings::cooperative | MemonSettings::polyphonic;
    game.configure(settings);
    game.start(23000, 1, hardware);
    assert(game.phase() == Memon::Phase::add && !game.countdown_visible());
    game.tick(25000, hardware);
    assert(game.phase() == Memon::Phase::add);
    game.press(2, 25000, hardware);
    assert(game.score() == 0 && game.lit() == 4);
    game.tick(25500, hardware);
    assert(game.round() == 2 && game.lit() == 4);
    game.tick(25750, hardware);
    game.tick(26000, hardware);
    assert(game.lit() == 2);
    game.tick(26250, hardware);
    game.press(2, 26300, hardware);
    game.press(1, 26400, hardware);
    assert(game.score() == 2 && game.phase() == Memon::Phase::add);
    game.press(0, 26500, hardware);
    assert(game.score() == 2 && game.phase() == Memon::Phase::next_round);

    game.leave(26600, hardware);
    settings.flags = MemonSettings::polyphonic;
    game.configure(settings);
    game.start(27000, 1, hardware);
    game.tick(27250, hardware);
    hardware.packets.clear();
    game.press(0, 27300, hardware);
    assert(hardware.packets.size() == 4);
    assert(hardware.packets.back() == Bytes({0x90, 70, 100, 0xb0, 2, 100}));
    hardware.packets.clear();
    game.tick(29300, hardware);
    assert(hardware.packets.size() == 4);
    assert(hardware.packets.back() == Bytes({0x80, 70, 0, 0xb0, 2, 0}));
}
