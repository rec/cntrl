#include <cntrl/midi_files.h>
#include <cntrl/expression_pedals.h>
#include "filesystem.h"
#include <cassert>
#include <cstring>

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
    assert(filesystem.make_directory("/cntrl"));
    {
        std::ofstream file(filesystem.path(cntrl::CalibrationFile::path));
        file << "settle_ms = 80\n[configurable-1]\nname = \"Boss EV-30, left\"\n"
            "[configurable-2]\nname = 'Right pedal'\nheel = 900\ntoe = 100\n";
    }
    cntrl::ExpressionPedals pedals(2, filesystem, 1, 1);
    assert(pedals.load_calibrations());
    cntrl::ExpressionAssignment assignments[2]{};
    pedals.configure(assignments);
    assert(pedals.calibrate());
    unsigned raw = 2000;
    auto cycle = [&](uint32_t now) {
        pedals.poll(now, [](unsigned jack) { return jack == 0; }, [&](unsigned, unsigned) { return int(raw); },
            [](const char *) { return true; }, [](unsigned, const char *, const uint8_t *) { return true; });
    };
    unsigned now = 0;
    for (unsigned position : {2000, 1300, 600, 1500, 2400, 3250, 3780}) {
        raw = position;
        const unsigned end = now + 40;
        while (now < end) cycle(++now);
    }
    assert(!pedals.capture(0).toe_ready);
    for (unsigned i = 0; i < 80; ++i) cycle(++now);
    assert(pedals.capture(0).toe_ready && pedals.calibrate());
    std::ifstream file(filesystem.path(cntrl::CalibrationFile::path));
    std::string text((std::istreambuf_iterator<char>(file)), {});
    assert(text.find("bits = 12\nsettle_ms = 80") == 0);
    assert(text.find("name = \"Boss EV-30, left\"\nheel = 600\ntoe = 3780") != text.npos);
    cntrl::ExpressionPedals recovered(2, filesystem, 1, 1);
    assert(recovered.load_calibrations());
    assert(!std::strcmp(recovered.name(0), "Boss EV-30, left"));
    assert(recovered.calibration(0).heel == 600 && recovered.calibration(0).toe == 3780);
    assert(recovered.calibration(1).heel == cntrl::ExpressionCalibration::from_10_bit(900));
    assert(recovered.calibration(1).toe == cntrl::ExpressionCalibration::from_10_bit(100));
}
void test_legacy_calibration_carries_forward(TestFileSystem &filesystem) {
    TestFileSystem legacy(filesystem.path("/legacy"));
    assert(legacy.make_directory("/cntrl"));
    uint8_t record[12]{};
    cntrl::put32(record, 1);
    cntrl::put32(record + 4, 921 | (103u << 16));
    cntrl::put32(record + 8, cntrl::checksum(record, 8));
    TestFile file(legacy);
    assert(file.open("/cntrl/expression-2.cal", cntrl::files::Mode::truncate));
    assert(file.write(0, record, sizeof(record)));
    file.close();

    cntrl::ExpressionPedals pedals(2, legacy, 1, 1);
    assert(pedals.load_calibrations());
    assert(pedals.calibration(1).heel == cntrl::ExpressionCalibration::from_10_bit(921));
    assert(pedals.calibration(1).toe == cntrl::ExpressionCalibration::from_10_bit(103));
}
int main(int argc, char **argv) {
    cntrl::memory_budget = 524288;
    assert(argc == 2);
    TestFileSystem filesystem(argv[1]);
    test_recording_selection(filesystem);
    test_calibration_survives_reload(filesystem);
    test_legacy_calibration_carries_forward(filesystem);
}
