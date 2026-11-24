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
    MemonHardware hardware;
    Memon game;
    MemonSettings settings;
    settings.flags |= MemonSettings::repeat;
    game.configure(settings);
    game.start(0, 1, hardware);
    const unsigned first = 1; // Seed 1 starts with the second tile.
    assert(game.lit() == (1u << first));
    game.tick(250, hardware);
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
    assert(hardware.packets.back().size() == 12 && hardware.packets.back()[0] == 0x90);
    hardware.blocked = true;
    game.leave(18020, hardware);
    hardware.blocked = false;
    game.tick(18040, hardware);
    assert(hardware.packets.back().size() == 12 && hardware.packets.back()[0] == 0x80);

    settings.flags = MemonSettings::cooperative;
    game.configure(settings);
    game.start(19000, 1, hardware);
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
}
