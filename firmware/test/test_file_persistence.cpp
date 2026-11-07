#include <cntrl/midi_files.h>
#include <cntrl/expression_pedals.h>
#include "filesystem.h"
#include <cassert>
#include <cstring>

void calibration_name(unsigned index, char *path, unsigned capacity) {
    std::snprintf(path, capacity, "/calibration-%u", index);
}
void test_recording_selection(TestFileSystem &filesystem) {
    TestFile source(filesystem), index(filesystem), recording(filesystem);
    TestDirectory directory(filesystem);
    cntrl::midi::MidiFiles files(filesystem, source, index, recording, directory, 1, "/music", "/music/index");
    assert(!files.record({}, 255, false, 0));
    assert(files.record({}, 255, true, 0));
    assert(!files.play(0, {})); // An unfinished recording cannot be selected.
    files.receive(0, uint8_t(0x90), 0); files.receive(0, uint8_t(60), 0); files.receive(0, uint8_t(100), 0);
    files.receive(7, uint8_t(0x90), 0); // Unknown inputs do not index parser memory.
    assert(files.record({}, 255, true, 100));
    assert(filesystem.exists("/music/255-1.mid"));
    assert(files.play(0, {}));
    files.player.cancel();
    assert(files.record({}, 255, true, 200));
    assert(files.record({}, 255, true, 300));
    assert(filesystem.exists("/music/255-2.mid"));
    files.boot();
    assert(files.play(0, {}));
    files.player.cancel();
    TestFile newest(filesystem); assert(newest.open("/music/255-2.mid", cntrl::files::Mode::read));
    assert(source.size() == newest.size());
}
void test_calibration_survives_reload(TestFileSystem &filesystem) {
    TestFile file(filesystem);
    cntrl::ExpressionPedals pedals(1, file, calibration_name, "Confirm", 8, 2);
    cntrl::ExpressionAssignment assignment{};
    pedals.configure(&assignment);
    pedals.calibrate(true);
    unsigned raw = 100;
    auto cycle = [&](uint32_t now) {
        pedals.poll(now, [](unsigned) { return true; }, [&](unsigned, unsigned) { return int(raw); },
            [](const char *) { return true; }, [](const char *, const uint8_t *) { return true; });
    };
    for (unsigned now = 8; now <= 44; now += 2) cycle(now);
    pedals.calibrate(true); // Capture heel.
    raw = 900;
    for (unsigned now = 46; now <= 500; now += 2) cycle(now);
    pedals.calibrate(true); // Capture toe and persist both endpoints.
    assert(filesystem.exists("/calibration-0"));
    TestFile reopened(filesystem);
    cntrl::ExpressionPedals recovered(1, reopened, calibration_name, "Confirm", 8, 2);
    recovered.load_calibrations(); recovered.configure(&assignment);
    for (unsigned now = 8; now <= 44; now += 2)
        recovered.poll(now, [](unsigned) { return true; }, [](unsigned, unsigned) { return 500; },
            [](const char *) { return true; }, [](const char *, const uint8_t *) { return true; });
    char title[40], position[40], prompt[40];
    recovered.status("Pedal", true, title, position, prompt);
    assert(std::strstr(position, "Calibrated"));
    assert(!std::strstr(position, "Uncalibrated"));
    assert(!std::strcmp(prompt, "Confirm: Calibrate"));
}
int main(int argc, char **argv) {
    assert(argc == 2);
    TestFileSystem filesystem(argv[1]);
    test_recording_selection(filesystem);
    test_calibration_survives_reload(filesystem);
}
