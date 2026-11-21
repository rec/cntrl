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
        file << "[configurable-1]\nname = \"Boss EV-30, left\"\n"
            "[configurable-2]\nname = 'Right pedal'\nheel = 900\ntoe = 100\n";
    }
    cntrl::ExpressionPedals pedals(2, filesystem, "Confirm", 8, 2);
    assert(pedals.load_calibrations());
    cntrl::ExpressionAssignment assignments[2]{};
    pedals.configure(assignments);
    pedals.calibrate(true);
    unsigned raw = 100;
    auto cycle = [&](uint32_t now) {
        pedals.poll(now, [](unsigned) { return true; }, [&](unsigned, unsigned) { return int(raw); },
            [](const char *) { return true; }, [](unsigned, const char *, const uint8_t *) { return true; });
    };
    for (unsigned now = 8; now <= 44; now += 2) cycle(now);
    pedals.calibrate(true); // Capture heel.
    raw = 900;
    for (unsigned now = 46; now <= 500; now += 2) cycle(now);
    pedals.calibrate(true); // Capture toe and persist both endpoints.
    std::ifstream file(filesystem.path(cntrl::CalibrationFile::path));
    std::string text((std::istreambuf_iterator<char>(file)), {});
    assert(text.find("name = \"Boss EV-30, left\"\nheel = 100\ntoe = 899") != text.npos);
    assert(text.find("name = 'Right pedal'\nheel = 900\ntoe = 100") != text.npos);
    cntrl::ExpressionPedals recovered(2, filesystem, "Confirm", 8, 2);
    assert(recovered.load_calibrations()); recovered.configure(assignments);
    for (unsigned now = 8; now <= 44; now += 2)
        recovered.poll(now, [](unsigned) { return true; }, [](unsigned, unsigned) { return 500; },
            [](const char *) { return true; }, [](unsigned, const char *, const uint8_t *) { return true; });
    char title[40], position[40], prompt[40];
    recovered.status("Pedal", true, title, position, prompt);
    assert(std::strstr(position, "Calibrated"));
    assert(!std::strstr(position, "Uncalibrated"));
    assert(!std::strcmp(prompt, "Confirm: Calibrate"));
}
void test_calibration_error_follows_the_current_language(TestFileSystem &filesystem) {
    cntrl::ExpressionPedals pedals(1, filesystem, "Confirm", 8, 2);
    pedals.calibrate(true); pedals.calibrate(true); // Retain the no-pedal error.
    char title[40], position[40], prompt[40];
    pedals.status("Pedal", true, title, position, prompt);
    assert(!std::strcmp(prompt, "No pedal"));
    cntrl::ui::i18n::lookup = [](cntrl::ui::i18n::Message message, bool) -> const char * {
        return message == cntrl::ui::i18n::Message::no_pedal ? "Aucune pédale" : nullptr;
    };
    pedals.status("Pedal", true, title, position, prompt);
    assert(!std::strcmp(prompt, "Aucune pédale") && !std::strcmp(position, "Aucune pédale"));
    cntrl::ui::i18n::lookup = nullptr;
    pedals.status("Pedal", true, title, position, prompt);
    assert(!std::strcmp(prompt, "No pedal"));
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

    cntrl::ExpressionPedals pedals(2, legacy, "Confirm", 1, 1);
    assert(pedals.load_calibrations());
    unsigned raw = 118;
    auto sample = [&](unsigned first, unsigned end) {
        for (unsigned now = first; now < end; ++now)
            pedals.poll(now, [](unsigned) { return true; }, [&](unsigned, unsigned) { return int(raw); },
                [](const char *) { return true; }, [](unsigned, const char *, const uint8_t *) { return true; });
    };
    pedals.calibrate(true);
    sample(1, 30);
    pedals.calibrate(true);
    for (raw = 218; raw < 906; raw += 100) sample(raw, raw + 24);
    raw = 906;
    sample(1000, 1100);
    pedals.calibrate(true);
    std::ifstream saved(legacy.path(cntrl::CalibrationFile::path));
    std::string text((std::istreambuf_iterator<char>(saved)), {});
    assert(text.find("[configurable-2]\nheel = 921\ntoe = 103") != text.npos);
}
int main(int argc, char **argv) {
    cntrl::memory_budget = 524288;
    assert(argc == 2);
    TestFileSystem filesystem(argv[1]);
    test_recording_selection(filesystem);
    test_calibration_survives_reload(filesystem);
    test_calibration_error_follows_the_current_language(filesystem);
    test_legacy_calibration_carries_forward(filesystem);
}
