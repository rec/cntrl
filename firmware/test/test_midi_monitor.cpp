#include <cntrl/midi_monitor.h>
#include <cntrl/ui/pages.h>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

using Monitor = cntrl::midi::Monitor;
using Direction = Monitor::Direction;

struct Sink : cntrl::midi::Sink {
    Monitor &monitor;
    bool accepting = true;
    unsigned writes = 0;
    explicit Sink(Monitor &value) : monitor(value) {}
    bool write_packet(unsigned, const cntrl::midi::Packet &) override { ++writes; return accepting; }
    void packet_sent(unsigned route, const cntrl::midi::Packet &packet) override { monitor.packet(Direction::outgoing, route, 0, packet, 20); }
};
std::string summary(const Monitor &monitor, uint64_t position) {
    const auto *entry = monitor.entry(position); assert(entry);
    char text[64]; Monitor::describe(*entry, text, sizeof(text)); return text;
}
void receive(Monitor &monitor, std::initializer_list<uint8_t> bytes, unsigned route = 0, unsigned cable = 0, uint32_t now = 10) {
    for (uint8_t byte : bytes) monitor.byte(Direction::incoming, route, cable, byte, now);
}

void test_every_message_category_and_literal_releases() {
    Monitor monitor; assert(monitor.open(0));
    const std::vector<std::vector<uint8_t>> messages = {
        {0x8f,60,64}, {0x9f,60,0}, {0xaf,60,100}, {0xbf,64,127}, {0xcf,127},
        {0xdf,99}, {0xef,0,64}, {0xf1,0x12}, {0xf2,1,2}, {0xf3,4}, {0xf6},
        {0xf8}, {0xfa}, {0xfb}, {0xfc}, {0xfe}, {0xff},
    };
    for (const auto &message : messages) for (uint8_t byte : message) monitor.byte(Direction::incoming, 0, 0, byte, 10);
    assert(monitor.tail() == messages.size());
    for (unsigned i = 0; i < messages.size(); ++i) {
        const auto *entry = monitor.entry(i); assert(entry && entry->size == messages[i].size());
        assert(!std::memcmp(entry->bytes, messages[i].data(), entry->size));
    }
    assert(summary(monitor, 0) == "Ch16 Off 60 v64");
    assert(summary(monitor, 1) == "Ch16 On 60 v0");
    assert(summary(monitor, 4) == "Ch16 Program 128");
    assert(summary(monitor, 6) == "Ch16 Bend 0");
    assert(summary(monitor, 8) == "Song position 257");
    assert(summary(monitor, 11) == "Clock");
    assert(summary(monitor, 15) == "Active sensing");
}

void test_running_status_cables_and_malformed_input_are_independent() {
    Monitor monitor; assert(monitor.open(0));
    receive(monitor, {0x90,60}, 6, 1);
    receive(monitor, {0xb1,7,80}, 6, 2);
    receive(monitor, {100,61,0}, 6, 1);
    assert(monitor.tail() == 3);
    assert(summary(monitor, 0) == "Ch2 CC 7 = 80");
    assert(summary(monitor, 1) == "Ch1 On 60 v100");
    assert(summary(monitor, 2) == "Ch1 On 61 v0");
    assert(monitor.entry(2)->flags & Monitor::running);
    receive(monitor, {1,0xf4,0xf9,0xfd}, 3);
    for (unsigned i = 3; i < 7; ++i) assert(monitor.entry(i)->flags & Monitor::raw);
    receive(monitor, {0x90,62,0xc0,4});
    assert(summary(monitor, 7) == "raw / incomplete");
    assert(summary(monitor, 8) == "Ch1 Program 5");
}

void test_sysex_chunks_realtime_and_partial_input() {
    Monitor monitor; assert(monitor.open(0));
    receive(monitor, {0xf0,1,0xf8,2,3,4,5,6,0xf7});
    assert(monitor.tail() == 4);
    assert(summary(monitor, 0) == "Clock");
    assert(summary(monitor, 1) == "SysEx start 3B");
    assert(summary(monitor, 2) == "SysEx cont 3B");
    assert(summary(monitor, 3) == "SysEx cont/end 2B");
    assert((monitor.entry(1)->bytes[0] == 0xf0 && monitor.entry(1)->bytes[1] == 1 && monitor.entry(1)->bytes[2] == 2));
    receive(monitor, {0xf0,9,0x90,60,1});
    assert(summary(monitor, 4) == "SysEx raw / incomplete");
    assert(summary(monitor, 5) == "Ch1 On 60 v1");
    receive(monitor, {0x90,70});
    for (unsigned i = 0; i < 64; ++i) monitor.poll(300000);
    assert(summary(monitor, 6) == "raw / incomplete");
    assert(monitor.entry(6)->size == 2);
}

void test_successful_writes_only_and_closed_capture() {
    Monitor monitor; Sink sink(monitor); assert(monitor.open(0));
    sink.accepting = false;
    for (unsigned i = 0; i < 10; ++i) assert(!sink.submit(2, {11, {0xb0,7,80}}));
    assert(monitor.tail() == 0);
    sink.accepting = true; assert(sink.submit(2, {11, {0xb0,7,80}}));
    assert(monitor.tail() == 1 && monitor.entry(0)->direction == Direction::outgoing);
    assert(summary(monitor, 0) == "Ch1 CC 7 = 80");
    monitor.close(); assert(sink.submit(2, {15, {0xf8,0,0}}));
    assert(monitor.tail() == 1 && !monitor.active());
    assert(monitor.open(30)); assert(monitor.tail() == 0);
}

void test_history_freeze_overflow_memory_and_rollover() {
    const unsigned before = cntrl::memory_used;
    Monitor monitor; assert(monitor.open(UINT32_MAX - 20));
    assert(monitor.memory_bytes() <= 16 * 1024);
    monitor.freeze(); receive(monitor, {0xf8}, 0, 0, UINT32_MAX - 10);
    assert(monitor.cursor() == 0 && !monitor.advanced());
    monitor.freeze(); receive(monitor, {0xf8}, 0, 0, 20);
    assert(monitor.entry(1)->time == 41);
    monitor.freeze(); monitor.browse(true); assert(monitor.cursor() == 1);
    for (unsigned i = 0; i < Monitor::capacity; ++i) receive(monitor, {0xf8}, 0, 0, 21 + i);
    assert(monitor.oldest() == 2 && monitor.overwritten() == 2);
    assert(monitor.cursor() == 3 && monitor.advanced() && !monitor.entry(1));
    monitor.browse(true); assert(monitor.cursor() == 3);
    monitor.browse(false); assert(monitor.cursor() == 4);
    monitor.freeze(); assert(monitor.cursor() == monitor.tail());
    monitor.close(); assert(cntrl::memory_used == before);
    unsigned budget = cntrl::memory_budget; cntrl::memory_budget = before;
    assert(monitor.open(0));
    receive(monitor, {0xf8}); assert(monitor.tail() == 0 && monitor.dropped() == 1);
    Sink sink(monitor); assert(sink.submit(0, {15,{0xf8,0,0}}) && sink.writes == 1);
    assert(monitor.dropped() == 2); cntrl::memory_budget = budget;
}

void test_connections_reset_partial_and_running_status() {
    Monitor monitor; assert(monitor.open(0));
    monitor.connection(8, true, 1, 1, 1);
    receive(monitor, {0x90,60}, 8, 3, 2);
    monitor.connection(8, false, 1, 2, 3);
    assert(summary(monitor, 0) == "raw / incomplete");
    assert(summary(monitor, 1) == "Disconnected");
    monitor.connection(8, true, 2, 3, 4);
    receive(monitor, {100}, 8, 3, 5);
    assert(summary(monitor, 2) == "Connected / changed");
    assert(summary(monitor, 3) == "raw / incomplete");
    assert(monitor.entry(3)->bytes[0] == 100);
}

void test_dense_clock_and_long_sysex_keep_memory_bounded() {
    Monitor monitor; assert(monitor.open(0)); const unsigned memory = cntrl::memory_used;
    receive(monitor, {0xf0});
    for (unsigned i = 0; i < 10000; ++i) {
        monitor.byte(Direction::incoming, 0, 0, uint8_t(i & 127), 11 + i);
        if (i % 64 == 0) monitor.byte(Direction::incoming, 0, 0, 0xf8, 11 + i);
    }
    receive(monitor, {0xf7}, 0, 0, 10012);
    assert(monitor.tail() == 3334 + 157 && monitor.overwritten() == monitor.tail() - Monitor::capacity);
    assert(monitor.entry(monitor.tail() - 1)->flags & Monitor::end);
    for (uint64_t sequence = monitor.oldest(); sequence < monitor.tail(); ++sequence) assert(monitor.entry(sequence)->size <= 3);
    assert(cntrl::memory_used > memory && monitor.memory_bytes() == cntrl::memory_used - memory);
    const unsigned full = cntrl::memory_used, budget = cntrl::memory_budget; cntrl::memory_budget = full;
    for (unsigned i = 0; i < 1000; ++i) receive(monitor, {0xf8});
    assert(cntrl::memory_used == full && !monitor.dropped()); cntrl::memory_budget = budget;
}

struct Fonts : cntrl::ui::Fonts {
    bool supports(unsigned, unsigned, uint32_t = 0) const override { return true; }
    cntrl::ui::Bounds bounds(unsigned, unsigned, uint32_t, const char *s) const override { return {0,0,int(std::strlen(s) * 3),5}; }
    bool glyph(unsigned, unsigned, uint32_t, const char *, unsigned, cntrl::ui::Glyph &) const override { return false; }
};
void test_window_layouts_history_and_bounded_refresh() {
    constexpr cntrl::midi::OutputConnection endpoints[] = {{"keys", "Keys", 0, cntrl::midi::OutputTransport::physical, true}};
    Monitor monitor; assert(monitor.open(0)); receive(monitor, {0xc0,127});
    monitor.packet(Direction::outgoing, 0, 0, {11,{0xb0,7,80}}, 20, true);
    Fonts fonts; cntrl::ui::Pages pages(fonts, {160,128,0,0});
    cntrl::PageSettings settings; settings.words[cntrl::PageSettings::title_height] = 8; pages.configure(settings);
    pages.outputs({endpoints, 1}); pages.monitor = &monitor;
    for (unsigned count = 1; count <= 3; ++count) {
        uint8_t layout[8 + 3 * 32 + 37] = {'C','P','A','G',1,3,1,0};
        layout[8 + 3 * 32 + 32] = unsigned(cntrl::ui::Pages::Page::midi_monitor);
        layout[8 + 3 * 32 + 33] = uint8_t(count); pages.layout({layout});
        for (unsigned window = 0; window < count; ++window) {
            auto pane = pages.view(100, 100000, {}, {}, cntrl::Panic::Status::ready, window, count);
            const bool combined = count == 1 || (count == 3 && window == 1);
            assert(!std::strcmp(pane.lines[0].text, combined ? "Combined" : window == 0 ? "Incoming" : "Outgoing"));
            std::string text;
            for (const auto &line : pane.lines) { text += line.text; text += '\n'; }
            assert((text.find("Program 128") != std::string::npos) == (combined || window == 0));
            assert((text.find("CC 7 = 80") != std::string::npos) == (combined || window + 1 == count));
            assert(!pages.due(199) && pages.due(200));
        }
    }
    assert(monitor.tail() == 2); // Extra views never capture duplicate observations.
}

int main() {
    cntrl::memory_budget = 128 * 1024;
    test_every_message_category_and_literal_releases();
    test_running_status_cables_and_malformed_input_are_independent();
    test_sysex_chunks_realtime_and_partial_input();
    test_successful_writes_only_and_closed_capture();
    test_history_freeze_overflow_memory_and_rollover();
    test_connections_reset_partial_and_running_status();
    test_dense_clock_and_long_sysex_keep_memory_bounded();
    test_window_layouts_history_and_bounded_refresh();
    assert(cntrl::memory_used == 0);
}
