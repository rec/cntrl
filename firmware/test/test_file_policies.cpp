#include <cntrl/asset_storage.h>
#include <cntrl/slot_storage.h>
#include <cntrl/storage_listing.h>
#include "filesystem.h"
#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    cntrl::memory_budget = 128 * 1024;
    assert(argc == 2); TestFileSystem filesystem(argv[1]);
    TestFile writer(filesystem); bool ready = false;
    const unsigned unused = cntrl::memory_used;
    cntrl::SlotStorage slots(filesystem, writer, 4, "/cntrl/songs", ready);
    assert(cntrl::memory_used == unused);
    const uint8_t first[] = {1, 2, 3}; uint8_t copied[3] = {};
    assert(!slots.begin(0) && slots.size(0) == 0);
    ready = true; assert(slots.begin(0) && slots.write(first, sizeof(first)) && slots.finish());
    assert(slots.size(0) == 3 && slots.read(0, 0, copied, 3) && !std::memcmp(first, copied, 3));
    const uint8_t second[] = {4, 5};
    assert(slots.begin(0) && slots.write(second, 2) && slots.finish());
    assert(slots.size(0) == 2 && slots.read(0, 0, copied, 2) && !std::memcmp(second, copied, 2));
    assert(!slots.begin(4) && !slots.read(4, 0, copied, 1));
    assert(slots.begin(1) && slots.write(first, sizeof(first)) && slots.finish());
    const unsigned reader_budget = cntrl::memory_budget;
    cntrl::memory_budget = cntrl::memory_used;
    assert(!slots.read(1, 0, copied, 3));
    assert(slots.read(0, 0, copied, 2) && !std::memcmp(second, copied, 2));
    cntrl::memory_budget = reader_budget;
    assert(slots.read(1, 0, copied, 3) && !std::memcmp(first, copied, 3));
    TestFile movie_reader(filesystem), movie_writer(filesystem);
    cntrl::AssetStorage assets(filesystem, movie_reader, movie_writer);
    uint8_t movie[24] = {}; std::memcpy(movie + 20, "DONE", 4);
    assert(!assets.begin(0xabcd)); assets.ready = true;
    assert(assets.begin(0xabcd) && assets.write(movie, 12) && !assets.finish());
    assert(!filesystem.exists("/cntrl/movies/0000abcd.bin"));
    assert(assets.begin(0xabcd) && assets.write(movie, 24) && assets.finish());
    assert(assets.size(0xabcd) == 24 && assets.read(0xabcd, 20, copied, 3) && !std::memcmp(copied, "DON", 3));
    movie[0] = 42;
    assert(assets.begin(0xabcd) && assets.write(movie, 24) && !assets.finish());
    assert(assets.read(0xabcd, 0, copied, 1) && copied[0] == 0);
    std::ofstream(filesystem.path("/name café.bin"), std::ios::binary) << "test";
    std::ofstream(filesystem.path(("/" + std::string(255, 'x')).c_str()), std::ios::binary) << "long";
    cntrl::StorageListing listing(filesystem);
    const cntrl::StorageListing::Detail details[] = {{"filesystem", "LittleFS"}, {"product", "Internal flash"}};
    assert(listing.begin(details, 2));
    const unsigned budget = cntrl::memory_budget; cntrl::memory_budget = cntrl::memory_used;
    assert(!listing.begin()); cntrl::memory_budget = budget;
    char response[600]; unsigned records = 0;
    listing.next(response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL DETAIL 66696c6573797374656d 4c6974746c654653"));
    listing.next(response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL DETAIL 70726f64756374 496e7465726e616c20666c617368"));
    do {
        listing.next(response, sizeof(response)); std::cout << response << '\n';
        assert(std::strncmp(response, "CNTRL ERROR", 11)); assert(++records < 100);
    } while (std::strncmp(response, "CNTRL STORAGE END ", 18));
    assert(listing.begin()); char short_response[80];
    for (unsigned i = 0; i < 100; ++i) {
        listing.next(short_response, sizeof(short_response));
        if (!std::strcmp(short_response, "CNTRL ERROR storage-list")) return 0;
    }
    assert(false);
}
