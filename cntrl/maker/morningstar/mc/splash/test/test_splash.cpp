#include "../receiver.h"
#include "../color_cycle.h"
#include "storage.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

using namespace cntrl::splash;

struct Responses : Replies {
    std::vector<std::string> lines;
    void reply(const char *line) override { lines.emplace_back(line); }
};

struct Screen : Output {
    unsigned count = 0;
    uint8_t last_mask = 0, last_pixel = 0;
    unsigned last_y = 0;
    void row(uint8_t panels, unsigned y, const uint8_t *pixels) override {
        assert(panels == 0b101 || panels == 0b010);
        assert(y < height);
        last_mask = panels; last_y = y; last_pixel = pixels[0]; ++count;
    }
};

void settle(Receiver &receiver) {
    for (unsigned i = 0; receiver.busy() && i < 1000; ++i) receiver.poll(0);
    assert(!receiver.busy());
}

void command(Receiver &receiver, const std::string &line) {
    for (unsigned char c : line + "\n") receiver.receive(c, 0);
}

void upload(Receiver &receiver, const std::vector<uint8_t> &asset, uint32_t crc) {
    char line[96];
    std::snprintf(line, sizeof(line), "CNTRL SPLASH BEGIN %lu %08lx",
                  (unsigned long)asset.size(), (unsigned long)crc);
    command(receiver, line);
    if (!receiver.busy()) return;
    for (uint8_t b : asset) {
        receiver.receive(b, 0);
        if (!receiver.busy()) break; // Host stops sending after a rejected chunk.
    }
    settle(receiver);
}

int main() {
    Tint red = {63, 0, 0};
    assert(tint_white(0xffff, red) == 0xf800);
    for (unsigned pixel = 0; pixel < 65536; ++pixel)
        assert(tint_white(uint16_t(pixel), {63, 63, 63}) == pixel);
    ColorWalk walk;
    ColorWalk centre(0x243f6a89);
    ColorWalk repeat;
    Tint previous = walk.at(0);
    Tint previous_centre = centre.at(0);
    bool changed = false;
    bool independent = false;
    for (unsigned now = 0; now < 600000; now += 100) {
        Tint tint = walk.at(now);
        Tint other = centre.at(now);
        Tint matching = repeat.at(now);
        assert(matching.red == tint.red && matching.green == tint.green && matching.blue == tint.blue);
        assert(other.red <= 63 && other.green <= 63 && other.blue <= 63);
        independent |= other.red != tint.red || other.green != tint.green || other.blue != tint.blue;
        assert(tint.red <= 63 && tint.green <= 63 && tint.blue <= 63);
        const unsigned before[] = {previous.red, previous.green, previous.blue,
            previous_centre.red, previous_centre.green, previous_centre.blue};
        const unsigned after[] = {tint.red, tint.green, tint.blue, other.red, other.green, other.blue};
        for (unsigned i = 0; i < 6; ++i) {
            assert(after[i] <= before[i] + 5 && before[i] <= after[i] + 5);
            changed |= before[i] != after[i];
        }
        previous = tint;
        previous_centre = other;
        assert(tint_white(0, tint) == 0);
        for (unsigned green = 0; green < 64; ++green)
            assert(tint_white(uint16_t(green << 5), tint) == green << 5);
    }
    assert(changed && independent);
    ColorWalk wrapping;
    wrapping.at(0xffffff00);
    Tint wrapped = wrapping.at(0x54);
    assert(wrapped.red >= 59 && wrapped.green <= 4 && wrapped.blue <= 4);
    for (unsigned shade = 0; shade < 32; ++shade) {
        unsigned green = (shade << 1) | (shade >> 4);
        uint16_t grey = uint16_t(shade << 11 | green << 5 | shade);
        assert(tint_white(grey, red) == shade << 11);
    }
    uint16_t green_edge = uint16_t(16 << 11 | 47 << 5 | 16);
    assert(tint_white(green_edge, red) == (16 << 11 | 14 << 5));
    std::vector<uint8_t> asset(header_bytes + frame_bytes, 0);
    std::memcpy(asset.data(), "CNSA", 4);
    asset[4] = 1; asset[6] = 160; asset[8] = 128; asset[10] = 1;
    put32(asset.data() + 12, unsigned(asset.size()));
    put32(asset.data() + header_bytes, 100);
    // Distinct pre-oriented rows: firmware must not mirror/recolour them.
    for (unsigned row = 0; row < height * 2; ++row)
        std::fill_n(asset.data() + header_bytes + 4 + row * row_bytes, row_bytes, row & 1 ? 23 : 17);
    uint32_t crc = ~crc_update(0xffffffff, asset.data(), unsigned(asset.size()));
    Memory memory;
    Responses replies;
    Receiver receiver(memory, replies);
    receiver.boot(); settle(receiver);
    assert(receiver.size() == 0);
    upload(receiver, asset, crc);
    assert(replies.lines.back().find("CNTRL SPLASH DONE ") == 0);
    assert(receiver.size() == asset.size());
    assert(memory.maximum_read <= 512);
    std::vector<uint8_t> readback(asset.size());
    assert(receiver.read(0, readback.data(), unsigned(readback.size())) && readback == asset);
    command(receiver, "CNTRL SPLASH STATUS");
    assert(replies.lines.back().find("CNTRL SPLASH READY ") == 0);

    Screen screen;
    Player player(receiver, screen);
    assert(player.start(0xfffffff0));
    for (unsigned i = 0; i < height * 2; ++i) {
        assert(player.poll(0xfffffff0, false) == Step::playing);
        assert(screen.count == i + 1 && screen.last_y == i / 2);
        assert(screen.last_mask == (i & 1 ? 0b010 : 0b101));
        assert(screen.last_pixel == (i & 1 ? 23 : 17));
    }
    assert(player.poll(20, false) == Step::playing && screen.count == height * 2);
    assert(player.poll(84, false) == Step::playing && screen.count == height * 2 + 1);
    assert(player.poll(85, true) == Step::dismissed);
    unsigned before = screen.count;
    assert(player.poll(86, true) == Step::inactive && screen.count == before);

    // A corrupt transfer must leave the previously committed asset readable.
    auto corrupt = asset; corrupt.back() ^= 1;
    upload(receiver, corrupt, crc);
    assert(replies.lines.back() == "CNTRL SPLASH ERROR checksum");
    Receiver reboot(memory, replies); reboot.boot(); settle(reboot);
    assert(reboot.size() == asset.size());
    assert(reboot.read(0, readback.data(), unsigned(readback.size())) && readback == asset);

    // Interruption at envelope, payload, trailer and verification boundaries.
    auto newer = asset; newer.back() = 42;
    uint32_t new_crc = ~crc_update(0xffffffff, newer.data(), unsigned(newer.size()));
    for (unsigned limit : {0u, 15u, 16u, 17u, 528u, unsigned(asset.size()),
                          unsigned(asset.size()) + 16, unsigned(asset.size()) + 19}) {
        Memory interrupted = memory; interrupted.remaining = int(limit);
        Receiver writer(interrupted, replies); writer.boot(); settle(writer);
        upload(writer, newer, new_crc);
        Receiver recovery(interrupted, replies); recovery.boot(); settle(recovery);
        assert(recovery.size() == asset.size());
        assert(recovery.read(0, readback.data(), unsigned(readback.size())) && readback == asset);
    }
    upload(receiver, newer, new_crc);
    Receiver latest(memory, replies); latest.boot(); settle(latest);
    assert(latest.read(0, readback.data(), unsigned(readback.size())) && readback == newer);

    auto two_frames = asset;
    two_frames.insert(two_frames.end(), asset.begin() + header_bytes, asset.end());
    two_frames[10] = 2;
    put32(two_frames.data() + 12, unsigned(two_frames.size()));
    put32(two_frames.data() + header_bytes + frame_bytes, 200);
    two_frames[header_bytes + frame_bytes + 4] = 33;
    Memory two_memory;
    Receiver two(two_memory, replies); two.boot(); settle(two);
    upload(two, two_frames, ~crc_update(0xffffffff, two_frames.data(), unsigned(two_frames.size())));
    Screen two_screen;
    Player timed(two, two_screen);
    assert(timed.start(0));
    for (unsigned i = 0; i < height * 2; ++i) timed.poll(0, false);
    timed.poll(100, false);
    assert(two_screen.last_pixel == 33 && two_screen.last_y == 0);
    for (unsigned i = 1; i < height * 2; ++i) timed.poll(100, false);
    unsigned sent = two_screen.count;
    timed.poll(299, false); assert(two_screen.count == sent);
    timed.poll(300, false); assert(two_screen.last_pixel == 17 && two_screen.last_y == 0);
    two_memory.slots[two_memory.selected].resize(17);
    assert(timed.poll(301, false) == Step::failed);

    auto zero_duration = asset; put32(zero_duration.data() + header_bytes, 0);
    upload(receiver, zero_duration,
           ~crc_update(0xffffffff, zero_duration.data(), unsigned(zero_duration.size())));
    assert(replies.lines.back().find("CNTRL SPLASH ERROR ") == 0);
    command(receiver, "CNTRL SPLASH BEGIN 81940 00000000");
    receiver.poll(10001);
    assert(replies.lines.back() == "CNTRL SPLASH ERROR timeout");

    Memory broken = memory;
    broken.slots[0].resize(5); broken.slots[1].resize(5);
    Receiver empty(broken, replies); empty.boot(); settle(empty);
    Player unavailable(empty, screen); assert(!unavailable.start(0));
}
