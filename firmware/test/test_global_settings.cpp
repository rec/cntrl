#include <cntrl/global_settings.h>
#include <cntrl/global_commands.h>
#include <cntrl/bundled_storage.h>
#include "filesystem.h"
#include "patch_hardware.h"
#include "storage.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware, cntrl::Replies {
    cntrl::midi::OutputConnection outputs[9];
    Board() { std::copy(test_midi_outputs.begin(), test_midi_outputs.end(), outputs); outputs[7].output = true; }
    cntrl::midi::Outputs midi_outputs() const override { return {outputs, 9}; }
    unsigned brightness_writes = 0;
    uint8_t brightness = 0;
    bool resources = true;
    bool activity_busy = false;
    cntrl::GlobalCommands *globals = nullptr;
    const cntrl::GlobalSettings::Values *values = nullptr;
    std::string last_error, last_reply;
    const cntrl::GlobalSettings::Values *global_values() const override { return values; }
    bool global_settings_busy() override { return activity_busy; }
    bool global_setting(unsigned field, uint32_t value, bool clear) override { return globals && globals->request(field, value, clear); }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool supports_trs_mode(unsigned port, cntrl::midi::TrsMode mode) const override { return port < 4 && mode <= cntrl::midi::TrsMode::aux; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    bool prepare_pages(const cntrl::Document &) override { return resources; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t value, bool = false) override { brightness = value; ++brightness_writes; }
    void set_splash(bool, uint32_t) override {}
    void error(const char *message) override { last_error = message; }
    void reply(const char *message) override { last_reply = message; }
    void data(const uint8_t *, unsigned) override { assert(false); }
};
struct Files : TestFileSystem {
    using TestFileSystem::TestFileSystem;
    bool blocked = false;
    std::unique_ptr<cntrl::files::File> file() override { return blocked ? nullptr : TestFileSystem::file(); }
};

void journal_recovers_the_previous_valid_generation(const std::filesystem::path &directory) {
    Files filesystem(directory);
    cntrl::GlobalSettings settings(filesystem); assert(settings.boot());
    cntrl::GlobalSettings::Values value;
    assert(settings.candidate(0, 160, false, value) && settings.commit(value));
    cntrl::GlobalSettings rebooted(filesystem); assert(rebooted.boot());
    assert(rebooted.values().known == 1 && rebooted.values().values[0] == 160);
    assert(rebooted.candidate(0, 0, true, value) && rebooted.commit(value));
    assert(!rebooted.values().known);
    std::ofstream(filesystem.path("/cntrl/global/1.bin"), std::ios::binary | std::ios::trunc) << "broken";
    cntrl::GlobalSettings recovered(filesystem); assert(recovered.boot());
    assert(recovered.values().known == 1 && recovered.values().values[0] == 160);
    std::ofstream(filesystem.path("/cntrl/global/0.bin"), std::ios::binary | std::ios::trunc) << "broken";
    cntrl::GlobalSettings invalid(filesystem); assert(!invalid.boot());
}

void inherited_settings_remain_separate_from_authored_setups(const char *bundle, const std::filesystem::path &directory) {
    Board board; TestStorage disk; cntrl::BundledStorage storage(disk, board); assert(storage.boot());
    std::ifstream input(bundle, std::ios::binary);
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    assert(storage.upload_begin(unsigned(bytes.size()), cntrl::checksum(bytes.data(), unsigned(bytes.size())), 1));
    for (unsigned offset = 0; offset < bytes.size();) {
        const unsigned length = std::min(128u, unsigned(bytes.size()) - offset);
        assert(storage.upload_write(bytes.data() + offset, length)); offset += length;
    }
    assert(storage.upload_finish());
    Files filesystem(directory); cntrl::GlobalSettings settings(filesystem); assert(settings.boot());
    TestController controller(board); cntrl::Documents documents(storage, board, controller, board, &settings); documents.boot();
    cntrl::GlobalCommands commands(settings, documents, board, board);
    board.globals = &commands; board.values = &settings.values();
    assert(!std::strcmp(documents.selected(), "a") && documents.document().brightness() == 192);
    {
        const auto &original = documents.document();
        std::vector<uint8_t> clock_bytes(original.data(), original.data() + original.length());
        assert(clock_bytes[original.core_offset() + 19] & 0x20);
        const unsigned clock = unsigned(original.bank(original.patches()) - original.data())
            + ((clock_bytes[original.core_offset() + 19] & 0x80) ? cntrl::expression_section_size : 0);
        clock_bytes[clock] = 128; clock_bytes[clock + 1] = 0; // Clock on USB device return output.
        assert(cntrl::binary::seal_setup(clock_bytes.data(), unsigned(clock_bytes.size())));
        cntrl::Document clock_document;
        assert(clock_document.load(clock_bytes.data(), unsigned(clock_bytes.size()), board, nullptr));
        cntrl::GlobalSettings::Values replacement;
        replacement.known = uint64_t(1) << 3; replacement.values[3] = 0;
        assert(!clock_document.load(clock_bytes.data(), unsigned(clock_bytes.size()), board, &replacement));
        replacement.known |= uint64_t(1) << 28; // Disable the output and its clock atomically.
        assert(clock_document.load(clock_bytes.data(), unsigned(clock_bytes.size()), board, &replacement));
    }
    cntrl::Document cached;
    assert(cached.load(documents.document().data(), documents.document().length(), board));
    cntrl::PatchExecutor executor(board, board, board, 1);
    const unsigned brightness_patch = unsigned(documents.document().named_patch("global_brightness"));
    const unsigned buffers_patch = unsigned(documents.document().named_patch("global_buffers"));
    assert(executor.start(documents.document(), brightness_patch)); executor.tick(); executor.tick();
    assert(executor.status() == cntrl::PatchExecutor::Status::completed);
    assert(!settings.values().known && documents.document().brightness() == 192);
    commands.poll(false);
    assert(documents.document().brightness() == 170 && cached.brightness() == 170);
    board.activity_busy = true;
    assert(executor.start(documents.document(), buffers_patch)); executor.tick(); executor.tick();
    assert(executor.status() == cntrl::PatchExecutor::Status::failed && !(settings.values().known & 128));
    board.activity_busy = false;
    assert(executor.start(documents.document(), buffers_patch)); executor.tick(); executor.tick();
    assert(executor.status() == cntrl::PatchExecutor::Status::completed);
    commands.poll(true); // A continuing patch must not invalidate its own document.
    assert(!(settings.values().known & 128) && board.last_error == "Global setting rejected");
    assert(commands.request(7, 1, false));
    board.activity_busy = true; commands.poll(false); // Activity may begin after the request.
    assert(!(settings.values().known & 128));
    board.activity_busy = false;
    assert(commands.request(0, 171, false));
    assert(!commands.request(0, 172, false));
    assert(commands.command("CNTRL GLOBAL SET 0 173") && board.last_reply == "CNTRL ERROR global-busy");
    commands.poll(false);
    assert(settings.values().values[0] == 171);
    assert(commands.command("CNTRL GLOBAL SET 0 175 trailing") && board.last_reply == "CNTRL ERROR global-command");
    assert(commands.command("CNTRL GLOBAL PUT 0=175 0=176") && board.last_reply == "CNTRL ERROR global-values");
    assert(commands.command("CNTRL GLOBAL PUT 0=175 3=0") && board.last_reply == "CNTRL ERROR global-invalid-setup");
    assert(settings.values().known == 1 && cached.brightness() == 171);
    assert(commands.command("CNTRL GLOBAL PUT 0=174") && board.last_reply == "CNTRL OK");
    assert(cached.brightness() == 174);
    assert(documents.global_change(19, unsigned(cntrl::midi::TrsMode::midi_a), false, false));
    assert(cached.trs_modes()[0] == unsigned(cntrl::midi::TrsMode::midi_a));
    assert(documents.global_change(19, unsigned(cntrl::midi::TrsMode::midi_b), false, false));
    assert(cached.trs_modes()[0] == unsigned(cntrl::midi::TrsMode::midi_b));
    assert(documents.global_change(19, 0, true, false));
    assert(cached.trs_modes()[0] == cached.data()[cached.metadata() + cntrl::trs_modes_offset]);
    assert(documents.global_change(0, 160, false, true));
    assert(board.brightness == 160 && documents.document().data()[documents.document().core_offset() + 18] == 192);
    assert(!documents.global_change(7, 0, false, true) && !std::strcmp(documents.last_error(), "global-busy"));
    assert(!(settings.values().known & (uint64_t(1) << 7)));
    assert(!documents.global_change(2, 0, false, true) && !(settings.values().known & 4));
    assert(documents.global_change(7, 0, false, false) && documents.document().frame_buffers() == 0);
    assert(!documents.global_change(3, 0, false, false)); // The inactive setup uses USB output.
    assert(!std::strcmp(documents.last_error(), "global-invalid-setup") && !(settings.values().known & 8));
    assert(documents.select("b") && documents.document().brightness() == 90);
    const unsigned writes = board.brightness_writes;
    assert(documents.global_change(0, 180, false, false));
    assert(documents.document().brightness() == 90 && board.brightness_writes == writes);
    filesystem.blocked = true;
    assert(!documents.global_change(0, 179, false, false) && settings.values().values[0] == 180);
    filesystem.blocked = false;
    assert(documents.select("a") && documents.document().brightness() == 180);
    board.resources = false;
    assert(!documents.global_change(7, 1, false, false));
    assert(documents.document().frame_buffers() == 0 && settings.values().values[7] == 0);
    board.resources = true;
    assert(documents.edit_begin() && documents.edit_adjust(0, 5, 1) && documents.edit_save(0));
    for (unsigned i = 0; i < 10000 && documents.busy(); ++i) documents.tick(i);
    assert(!documents.busy() && documents.document().brightness() == 181 && (documents.document().explicit_settings() & 1));
    assert(documents.global_change(0, 150, false, false) && documents.document().brightness() == 181);
    assert(documents.global_change(0, 0, true, false) && documents.document().brightness() == 181);
    cntrl::GlobalSettings restarted(filesystem); assert(restarted.boot());
    assert(!(restarted.values().known & 1) && (restarted.values().known & (uint64_t(1) << 7)));
    assert(documents.save_trs_mode(0, cntrl::midi::TrsMode::midi_a, 0));
    for (unsigned i = 0; i < 10000 && documents.busy(); ++i) documents.tick(i);
    assert(!documents.busy() && (documents.document().explicit_settings() & (uint64_t(1) << 19)));
    assert(documents.global_change(19, unsigned(cntrl::midi::TrsMode::off), false, false));
    assert(documents.document().trs_modes()[0] == unsigned(cntrl::midi::TrsMode::midi_a));
}

int main(int argc, char **argv) {
    assert(argc == 3); cntrl::memory_budget = 16 * 1024 * 1024;
    const std::filesystem::path root(argv[2]);
    journal_recovers_the_previous_valid_generation(root / "journal");
    inherited_settings_remain_separate_from_authored_setups(argv[1], root / "settings");
}
