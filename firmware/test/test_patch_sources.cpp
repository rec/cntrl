#include "patch_hardware.h"
#include "storage.h"
#include <cntrl/patch_sources.h>
#include <cassert>
#include <fstream>

using Bytes = std::vector<uint8_t>;

struct SourceBoard : TestPatchHardware, cntrl::Replies {
    const cntrl::PatchExecutor *first = nullptr, *second = nullptr;
    std::string response;
    bool configuration_locked() const override { return (first && first->busy()) || (second && second->busy()); }
    unsigned switch_capacity() const override { return 5; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
    void reply(const char *text) override { response = text; }
};

Bytes record(const char *name, const Bytes &payload, unsigned generation = 1) {
    Bytes bytes(96); std::memcpy(bytes.data(), "CNST", 4);
    cntrl::put32(bytes.data() + 4, generation); cntrl::put32(bytes.data() + 8, unsigned(payload.size()));
    cntrl::put32(bytes.data() + 12, cntrl::checksum(payload.data(), unsigned(payload.size())));
    std::memcpy(bytes.data() + 20, name, std::strlen(name));
    cntrl::put32(bytes.data() + 92, cntrl::checksum(bytes.data(), 92));
    bytes.insert(bytes.end(), payload.begin(), payload.end()); bytes.insert(bytes.end(), {'D', 'O', 'N', 'E'}); return bytes;
}
Bytes external_reference(Bytes bytes, SourceBoard &board) {
    cntrl::Document document; assert(document.load(bytes.data(), unsigned(bytes.size()), board));
    unsigned position = unsigned(document.node(0) - bytes.data());
    unsigned graph = document.graph_metadata(), count = document.nodes();
    unsigned old = 4 + cntrl::u16(document.node(0) + 2), delta = 132 - old;
    bytes.insert(bytes.begin() + position + old, delta, 0);
    std::memset(bytes.data() + position, 0, 132);
    bytes[position] = 6; bytes[position + 2] = 128;
    std::memcpy(bytes.data() + position + 4, "borrowed", 8);
    std::memcpy(bytes.data() + position + 68, "Flute", 5);
    for (unsigned i = 1; i < count; ++i)
        cntrl::put32(bytes.data() + graph + 16 + i * 4, cntrl::u32(bytes.data() + graph + 16 + i * 4) + delta);
    cntrl::put32(bytes.data() + graph + 4, cntrl::u32(bytes.data() + graph + 4) + delta);
    cntrl::put32(bytes.data() + 8, unsigned(bytes.size()));
    cntrl::put32(bytes.data() + 20, cntrl::checksum(bytes.data() + 28, unsigned(bytes.size()) - 28)); return bytes;
}
int main(int argc, char **argv) {
    assert(argc == 2); std::ifstream file(argv[1]); std::string hex; file >> hex;
    Bytes original; for (unsigned i = 0; i < hex.size(); i += 2) original.push_back(uint8_t(std::stoul(hex.substr(i, 2), nullptr, 16)));
    SourceBoard board; TestStorage storage;
    storage.files["active"][0] = record("active", external_reference(original, board));
    storage.files["borrowed"][0] = record("borrowed", original);
    TestController controller(board); cntrl::Documents documents(storage, board, controller, board); documents.boot();
    assert(documents.document().valid() && !std::strcmp(documents.selected(), "active"));
    cntrl::StoredPatchSources sources(documents, board);
    cntrl::PatchExecutor first(board, board, sources, 0), second(board, board, sources, 1);
    board.first = &first; board.second = &second;
    assert(first.start(documents.document(), 0)); first.tick();
    assert(second.start(documents.document(), 0)); second.tick();
    const cntrl::Document *pinned = nullptr; uint32_t revision = 0;
    assert(sources.acquire("BORROWED", pinned, revision));
    assert(pinned->instrument(0)[64] == original[76 + 64]);
    assert(!documents.edit_begin() && !documents.select("borrowed"));
    for (char c : std::string("CNTRL SETUP SELECT 626f72726f776564\n")) documents.receive(uint8_t(c), 0);
    assert(documents.document().data() == controller.document().data());
    assert(!std::strcmp(documents.selected(), "active"));
    Bytes updated = original; updated[76 + 64] = 5;
    cntrl::put32(updated.data() + 20, cntrl::checksum(updated.data() + 28, unsigned(updated.size()) - 28));
    storage.files["borrowed"][0] = record("borrowed", updated, 2);
    const cntrl::Document *same = nullptr; uint32_t same_revision = 0;
    assert(sources.acquire("borrowed", same, same_revision) && same == pinned && same_revision == revision);
    first.tick(); assert(first.status() == cntrl::PatchExecutor::Status::completed);
    assert(sources.acquire("borrowed", same, same_revision) && same == pinned);
    second.tick(); assert(second.status() == cntrl::PatchExecutor::Status::completed);
    sources.begin();
    assert(sources.acquire("borrowed", same, same_revision) && same->instrument(0)[64] == 5 && same_revision != revision);
    const cntrl::Document *active = nullptr;
    assert(sources.acquire("ACTIVE", active, revision) && active->data() == documents.document().data());
    sources.end();
}
