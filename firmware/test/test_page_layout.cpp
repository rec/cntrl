#include "page_fixture.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

int main(int argc,char **argv) {
    cntrl::memory_budget = 1024 * 1024;
    if (argc == 2 && !std::strcmp(argv[1],"caps")) {
        for (unsigned type = 0; type <= cntrl::PageLayout::arpeggiator; ++type) {
            for (unsigned windows : {1u,3u,5u}) std::printf("%u ",cntrl::PageLayout::maximum_windows(type,windows));
            std::puts("");
        }
        return 0;
    }
    for (unsigned windows : {1u,2u,5u,17u,255u}) {
        auto bytes = page_fixture(windows,{11,1},{{0,1}});
        cntrl::PageLayout layout{bytes.data()}; cntrl::PageAllocation allocation;
        assert(allocation.prepare(layout)); allocation.finish(true); allocation.configure(layout);
        assert(layout.windows() == windows && layout.pages() == 2 && layout.length() == bytes.size());
        for (unsigned logical = 0; logical < windows; ++logical) assert(allocation.controller_window(0,logical) == int(logical));
        allocation.select(1); assert(allocation.region(0).count == 1 && !allocation.controller_visible(0));
    }
    for (bool left : {false,true}) {
        auto bytes = page_fixture(3,{11,12,1,10,11},{{0,2},{1,3,4}},left);
        cntrl::PageLayout layout{bytes.data()}; cntrl::PageAllocation allocation;
        assert(allocation.prepare(layout)); allocation.finish(true); allocation.configure(layout);
        assert(allocation.focus() == 0 && allocation.selected() == 0);
        assert(allocation.region(0).count == 2);
        assert(allocation.region(1).count == 1);
        assert(allocation.controller_window(0,0) == 0);
        assert(allocation.controller_window(0,2) == -1);
        allocation.select(3);
        assert(allocation.selected() == 3 && allocation.region(1).count == 0);
        assert(allocation.region(0).selected == 0 && allocation.region(0).count == 3);
        allocation.select(4); assert(allocation.controller_visible(0) && allocation.controller_visible(1));
        assert(allocation.controller_window(1,0) == int(allocation.region(1).first));
        allocation.select(2); assert(allocation.region(0).count == 1 && allocation.region(1).count == 2);
        allocation.select(3); assert(allocation.selected() == 3);
        assert(allocation.region(0).count == 1);
    }
    auto left = page_fixture(3,{11,12},{{0},{1}},true);
    auto right = page_fixture(3,{11,12},{{1},{0}},false);
    cntrl::PageAllocation a,b;
    assert(a.prepare({left.data()}) && b.prepare({right.data()})); a.finish(true); b.finish(true);
    a.configure({left.data()}); b.configure({right.data()});
    assert(a.region(0).count == b.region(1).count && a.region(1).count == b.region(0).count);
    for (unsigned type = 0; type <= cntrl::PageLayout::arpeggiator; ++type)
        assert(cntrl::PageLayout::maximum_windows(type,5) == (type == 10 ? 0u : type == 11 ? 5u : 1u));
    for (unsigned type = 0; type <= cntrl::PageLayout::arpeggiator; ++type) {
        if (type == 10 || type == 11) continue;
        for (unsigned saved : {0u,3u}) {
            auto bytes = page_fixture(5,{11,type},{{0},{1}});
            cntrl::PageLayout layout{bytes.data()};
            bytes[layout.record(1) - bytes.data() + 33] = uint8_t(saved);
            cntrl::PageAllocation allocation;
            assert(allocation.prepare(layout)); allocation.finish(true); allocation.configure(layout);
            assert(allocation.region(1).count == 1 && allocation.region(0).count == 4);
            assert(allocation.controller_window(0,3) == 3);
            assert(allocation.controller_window(0,4) == -1);
        }
    }
}
