#include <cntrl/animation/session.h>
#include <cntrl/animation/color_cycle.h>
#include "storage.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

using namespace cntrl::animation;
constexpr unsigned width = 17, height = 11, row_bytes = width * 2, frame_bytes = 4 + row_bytes * height * 2;

struct Responses : Replies {
    std::vector<std::string> lines;
    void reply(const char *line) override { lines.emplace_back(line); }
};

struct Screen : Output {
    unsigned count = 0;
    uint8_t last_mask = 0, last_pixel = 0;
    unsigned last_y = 0;
    bool begin(unsigned w, unsigned h, unsigned streams) override { return w == width && h == height && streams == 2; }
    bool ready() const override { return true; }
    bool pixels(unsigned stream, unsigned, unsigned y, const uint8_t *pixels, unsigned) override {
        assert(y < height);
        last_mask = uint8_t(stream); last_y = y; last_pixel = pixels[0]; ++count; return true;
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

unsigned choices = 0;
unsigned alternate(unsigned count) { return choices++ % count; }

void test_independent_movie_lists(Receiver &assets, uint32_t first, uint32_t second, uint32_t idle) {
    Screen screen; Player player(assets, screen);
    Session session(assets, player, alternate);
    uint8_t startup_keys[8], idle_key[4];
    put32(startup_keys, first); put32(startup_keys + 4, second); put32(idle_key, idle);
    session.movies(startup_keys, 2, idle_key, 1);
    choices = 0;
    session.configure(true, 100, 0);
    assert(session.poll(0) && screen.last_pixel == 17);
    session.configure(true, 100, 0);
    assert(session.poll(0) && screen.last_pixel == 42 && choices == 2);
    session.configure(false, 100, 0);
    session.take_redraw(); session.poll(0, true);
    assert(!session.poll(100, true));
    assert(session.brightness(192, 2100) == 48);
    assert(session.poll(4100)); // Start painting after the four-second fade.
    assert(session.poll(4100) && screen.last_pixel == 77 && choices == 3);
    for (unsigned i = 1; i < height * 2; ++i) session.poll(4100);
    assert(session.brightness(192, 4100) == 0);
    session.poll(6100);
    assert(session.brightness(192, 6100) == 48);
    session.movies(nullptr, 0, idle_key, 1);
    session.configure(true, 0, 0);
    assert(!session.poll(0, true) && choices == 3); // Empty startup is disabled.
    session.movies(startup_keys, 2, nullptr, 0);
    session.configure(false, 100, 0);
    assert(!session.poll(10000, true) && choices == 3); // Empty idle is disabled.
}

// A logical surface wider than a transfer packet must retain its pending bytes.
void test_bounded_async_packets() {
    struct Bytes : Source {
        std::vector<uint8_t> data = std::vector<uint8_t>(header_bytes + 4 + 333 * 3 * 2, 0);
        unsigned maximum = 0;
        uint32_t size() override { return uint32_t(data.size()); }
        bool read(uint32_t offset, uint8_t *out, uint32_t count) override {
            maximum = std::max(maximum, count);
            std::copy_n(data.data() + offset, count, out); return true;
        }
    } source;
    std::memcpy(source.data.data(), "CNSA", 4);
    source.data[4] = 2; source.data[6] = 77; source.data[7] = 1;
    source.data[8] = 3; source.data[10] = source.data[12] = source.data[14] = 1;
    put32(source.data.data() + 16, source.size());
    put32(source.data.data() + header_bytes, 100);
    for (unsigned i = header_bytes + 4; i < source.size(); ++i) source.data[i] = uint8_t(i);
    struct Pending : Output {
        bool pending = false, reject = true;
        unsigned calls = 0, x = 0, y = 0, length = 0;
        const uint8_t *bytes = nullptr;
        bool begin(unsigned width, unsigned height, unsigned streams) override {
            return width == 333 && height == 3 && streams == 1;
        }
        bool ready() const override { return !pending; }
        bool pixels(unsigned stream, unsigned px, unsigned py, const uint8_t *data, unsigned count) override {
            assert(stream == 0 && count <= packet_bytes / 2);
            if (reject) { reject = false; return false; }
            assert(px == x && py == y);
            bytes = data; length = count * 2; pending = true; ++calls;
            x += count; if (x == 333) { x = 0; ++y; }
            return true;
        }
    } output;
    Player player(source, output);
    assert(player.start(0));
    for (unsigned i = 0; i < 10; ++i) {
        assert(player.poll(0, false) == Step::playing);
        if (!output.pending) continue;
        std::vector<uint8_t> held(output.bytes, output.bytes + output.length);
        unsigned calls = output.calls;
        player.poll(0, false);
        assert(output.calls == calls && std::equal(held.begin(), held.end(), output.bytes));
        output.pending = false;
    }
    assert(output.calls == 9 && player.frame_ready() && source.maximum <= packet_bytes);
}

int main() {
    test_bounded_async_packets();
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
    asset[4] = 2; asset[6] = width; asset[8] = height; asset[10] = 2; asset[12] = 1; asset[14] = 1;
    put32(asset.data() + 16, unsigned(asset.size()));
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
        assert(screen.last_mask == (i & 1));
        assert(screen.last_pixel == (i & 1 ? 23 : 17));
    }
    assert(player.poll(20, false) == Step::playing && screen.count == height * 2);
    assert(player.poll(84, false) == Step::playing && screen.count == height * 2 + 1);
    assert(player.poll(85, true) == Step::dismissed);
    unsigned before = screen.count;
    assert(player.poll(86, true) == Step::inactive && screen.count == before);

    // A corrupt transfer must leave the previously committed asset readable.
    auto corrupt = asset; corrupt.back() ^= 1;
    upload(receiver, corrupt, crc ^ 1u);
    assert(replies.lines.back() == "CNTRL SPLASH ERROR checksum");
    Receiver reboot(memory, replies); reboot.boot(); settle(reboot);
    assert(reboot.choose(crc));
    assert(reboot.size() == asset.size());
    assert(reboot.read(0, readback.data(), unsigned(readback.size())) && readback == asset);

    // Interruption at envelope, payload, trailer and verification boundaries.
    auto newer = asset; newer.back() = 42; newer[header_bytes + 4] = 42;
    uint32_t new_crc = ~crc_update(0xffffffff, newer.data(), unsigned(newer.size()));
    for (unsigned limit : {0u, 15u, 16u, 17u, 528u, unsigned(asset.size()),
                          unsigned(asset.size()) + 16, unsigned(asset.size()) + 19}) {
        Memory interrupted = memory; interrupted.remaining = int(limit);
        Receiver writer(interrupted, replies); writer.boot(); settle(writer);
        upload(writer, newer, new_crc);
        Receiver recovery(interrupted, replies); recovery.boot(); settle(recovery);
        assert(recovery.choose(crc));
        assert(recovery.size() == asset.size());
        assert(recovery.read(0, readback.data(), unsigned(readback.size())) && readback == asset);
    }
    upload(receiver, newer, new_crc);
    Receiver latest(memory, replies); latest.boot(); settle(latest);
    assert(latest.choose(new_crc));
    assert(latest.read(0, readback.data(), unsigned(readback.size())) && readback == newer);
    auto idle = asset; idle[header_bytes + 4] = 77;
    uint32_t idle_crc = ~crc_update(0xffffffff, idle.data(), unsigned(idle.size()));
    upload(receiver, idle, idle_crc);
    test_independent_movie_lists(receiver, crc, new_crc, idle_crc);

    auto two_frames = asset;
    two_frames.insert(two_frames.end(), asset.begin() + header_bytes, asset.end());
    two_frames[12] = 2;
    put32(two_frames.data() + 16, unsigned(two_frames.size()));
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
    broken.slots[crc].resize(5);
    Receiver empty(broken, replies); empty.boot(); settle(empty);
    assert(!empty.choose(crc));
    Player unavailable(empty, screen); assert(!unavailable.start(0));
}
