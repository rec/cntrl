#include <cntrl/setup_table.h>
#include <cntrl/bundled_storage.h>
#include <cntrl/patch_sources.h>
#include "patch_hardware.h"
#include "storage.h"
#include <fstream>
#include <iterator>
#include <vector>

struct Board : TestPatchHardware, cntrl::Replies {
    unsigned sent = 0;
    void reply(const char *) override {}
    bool send(const char *, const uint8_t *, unsigned) override { ++sent; return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override {}
};


struct File : cntrl::SetupBundleFile {
    std::vector<uint8_t> bytes;
    unsigned reads = 0;
    explicit File(const char *path) {
        std::ifstream input(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(input), {});
    }
    uint32_t size() const override { return uint32_t(bytes.size()); }
    bool read(uint32_t offset, uint8_t *data, unsigned length) override {
        ++reads;
        if (offset > size() || length > size() - offset) return false;
        std::memcpy(data, bytes.data() + offset, length); return true;
    }
};

void upload(cntrl::BundledStorage &storage, File &file) {
    assert(storage.upload_begin(file.size(), cntrl::checksum(file.bytes.data(), file.size()), 1));
    for (unsigned offset = 0; offset < file.size();) {
        unsigned n = std::min(128u, file.size() - offset);
        assert(storage.upload_write(file.bytes.data() + offset, n)); offset += n;
    }
    assert(storage.upload_finish());
}

void test_saving_and_history(File &file, Board &board) {
    TestStorage disk;
    cntrl::BundledStorage storage(disk, board);
    assert(storage.boot()); upload(storage, file);
    TestController controller(board);
    cntrl::Documents documents(storage, board, controller, board);
    documents.boot();
    assert(!std::strcmp(documents.selected(), "a") && documents.document().brightness() == 100);
    assert(documents.edit_begin() && documents.edit_adjust(0, 5, 5) && documents.edit_save(0));
    for (unsigned i = 0; i < 10000 && documents.busy(); ++i) documents.tick(i);
    assert(!documents.busy() && !documents.dirty() && documents.document().brightness() == 105);
    assert(documents.history_restore("a", 1, 0));
    for (unsigned i = 0; i < 10000 && documents.busy(); ++i) documents.tick(i);
    assert(!documents.busy() && documents.document().brightness() == 100);
    assert(documents.select("b") && documents.document().brightness() == 101);
    const auto *pinned = storage.setups().get(0); storage.setups().pin(0);
    assert(storage.upload_begin(file.size(), cntrl::checksum(file.bytes.data(), file.size()), 2));
    assert(storage.upload_write(file.bytes.data(), file.size()));
    assert(!storage.upload_finish());
    assert(storage.setups().get(0) == pinned);
    storage.setups().unpin(0);
    // A partially written replacement must leave the previous bundle bootable.
    disk.remaining = 110;
    assert(storage.upload_begin(file.size(), cntrl::checksum(file.bytes.data(), file.size()), 2));
    assert(!storage.upload_write(file.bytes.data(), file.size())); storage.upload_cancel();
    disk.remaining = -1;
    cntrl::BundledStorage rebooted(disk, board);
    assert(rebooted.boot());
    assert(rebooted.setups().get(0)->brightness() == 100);
}

void test_history_after_reindex(File &first, File &second, Board &board) {
    TestStorage disk; cntrl::BundledStorage storage(disk, board);
    assert(storage.boot()); upload(storage, first);
    TestController controller(board); cntrl::Documents documents(storage, board, controller, board);
    documents.boot();
    assert(documents.history_save("a", "before-table-change"));
    for (unsigned i = 0; i < 10000 && documents.busy(); ++i) documents.tick(i);
    assert(!documents.busy()); upload(storage, second); documents.boot();
    assert(documents.history_restore("a", 1, 0));
    for (unsigned i = 0; i < 10000 && documents.busy(); ++i) documents.tick(i);
    assert(!documents.busy());
    const auto &document = documents.document(); int root = document.named_patch("borrowed");
    assert(root >= 0 && document.node(unsigned(root))[0] == 6);
    assert(cntrl::u16(document.node(unsigned(root)) + 4) == 2);
    cntrl::StoredPatchSources sources(storage.setups());
    cntrl::PatchExecutor executor(board, board, sources, 0);
    assert(executor.start(document, unsigned(root)));
    for (unsigned i = 0; i < 100 && executor.busy(); ++i) executor.tick(i);
    assert(executor.status() == cntrl::PatchExecutor::Status::completed);
    // An old captured source must not reinterpret index 1 as the new setup 'a'.
    unsigned start = 40 + 2 * 76 + 6, length = cntrl::u32(first.bytes.data() + 40 + 64);
    cntrl::Document stale; assert(stale.load(first.bytes.data() + start, length, board));
    cntrl::PatchExecutor blocked(board, board, sources, 1); unsigned sent = board.sent;
    assert(!blocked.start(stale, unsigned(stale.named_patch("borrowed"))));
    assert(board.sent == sent);
}

int main(int argc, char **argv) {
    assert(argc == 3 || argc == 4);
    Board board; File file(argv[1]); cntrl::SetupTable table;
    if (argc == 4) { File second(argv[3]); test_history_after_reindex(file, second, board); return 0; }
    test_saving_and_history(file, board);
    unsigned initial_budget = cntrl::memory_budget;
    assert(cntrl::memory_used == 0);
    unsigned setup_length = cntrl::u32(file.bytes.data() + 40 + 64);
    if (argv[2][0] == '0') cntrl::memory_budget = 2 * setup_length;
    assert(table.open(file, board));
    assert(cntrl::memory_used >= table.resident_bytes());
    cntrl::MemoryBuffer competing;
    assert(!competing.resize(cntrl::memory_budget - cntrl::memory_used + 1));
    assert(table.count() == 3 && table.index("B") == 1 && table.index("missing") == -1);
    assert(!std::strcmp(table.name(0), "a") && !std::strcmp(table.name(2), "c"));
    assert(table.all_resident() == (argv[2][0] == '1'));
    const auto *first = table.get(0); assert(first && first->brightness() == 100);
    unsigned length = first->length(), reads = file.reads;
    assert(table.get(0) == first && file.reads == reads);
    table.pin(0);
    if (table.all_resident()) {
        assert(table.total_setup_ram() == table.resident_bytes());
        assert(table.get(1)->brightness() == 101 && table.get(2)->brightness() == 102);
        assert(file.reads == reads);
    } else {
        assert(table.total_free_ram2() == 2 * length);
        assert(table.resident_bytes() == 2 * length);
        assert(table.get(2)->brightness() == 102 && file.reads > reads);
        assert(table.get(0) == first); // Eviction did not invalidate a pinned view.
        table.pin(2); reads = file.reads;
        assert(!table.get(1) && file.reads == reads); // No evictable entry fits.
        table.unpin(0);
        assert(table.get(1)->brightness() == 101 && file.reads > reads);
        table.unpin(2);
        assert(table.resident_bytes() <= table.total_free_ram2());
        table.clear();
        assert(cntrl::memory_used == 0);
        cntrl::memory_budget = initial_budget;
        return 0;
    }
    table.unpin(0);
    table.clear();
    assert(cntrl::memory_used == 0);
    cntrl::memory_budget = initial_budget;
}
