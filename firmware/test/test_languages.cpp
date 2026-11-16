#include <cntrl/ui/languages.h>
#include "filesystem.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
    assert(argc == 4); cntrl::memory_budget = 4 * 1024 * 1024;
    TestFileSystem filesystem(argv[1]);
    std::filesystem::create_directories(filesystem.path("/cntrl/languages"));
    cntrl::ui::Languages languages(filesystem);
    using Result = cntrl::LanguagePreparation;
    using Message = cntrl::ui::i18n::Message;
    assert(languages.boot("en"));
    assert(!std::strcmp(cntrl::ui::i18n::text(Message::pages_and_tools), "Pages and tools"));
    assert(languages.prepare("fr") == Result::failed);
    languages.finish(false);
    std::filesystem::copy_file(argv[2], filesystem.path("/cntrl/languages/fr.bin"));
    std::filesystem::copy_file(argv[3], filesystem.path("/cntrl/languages/en.bin"));
    unsigned work = 0;
    auto result = languages.prepare("fr");
    while (result == Result::pending) {
        assert(!std::strcmp(cntrl::ui::i18n::text(Message::pages_and_tools), "Pages and tools"));
        result = languages.prepare("fr"); assert(++work < 4096);
    }
    assert(result == Result::ready && work > 10);
    languages.finish(true);
    assert(!std::strcmp(languages.current(), "fr"));
    assert(std::strcmp(cntrl::ui::i18n::text(Message::pages_and_tools), "Pages and tools"));
    result = languages.prepare("en");
    while (result == Result::pending) result = languages.prepare("en");
    assert(result == Result::ready); languages.finish(false);
    assert(!std::strcmp(languages.current(), "fr"));
    std::ifstream input(argv[2], std::ios::binary);
    std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
    bytes[bytes.size() - 1] ^= 1;
    std::ofstream corrupt(filesystem.path("/cntrl/languages/fr.bin"), std::ios::binary | std::ios::trunc);
    corrupt.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); corrupt.close();
    // A changed checksum is observed when switching from another locale.
    result = languages.prepare("en");
    while (result == Result::pending) result = languages.prepare("en");
    assert(result == Result::ready); languages.finish(true);
    result = languages.prepare("fr");
    while (result == Result::pending) result = languages.prepare("fr");
    assert(result == Result::failed); languages.finish(false);
    assert(!std::strcmp(languages.current(), "en"));
    assert(!languages.boot("fr") && !std::strcmp(languages.current(), "en"));
}
