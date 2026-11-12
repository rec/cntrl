#pragma once
#include <cntrl/page_layout.h>
#include <cstdio>
#include <cstring>
#include <vector>

// Build page-layout records for focused UI and allocation tests.
inline std::vector<uint8_t> page_fixture(unsigned windows, const std::vector<unsigned> &types,
        const std::vector<std::vector<unsigned>> &regions, bool left_wins = false) {
    std::vector<uint8_t> bytes(12 + windows * 32 + types.size() * (35 + windows) + regions.size() * 34);
    std::memcpy(bytes.data(),"CPAG",4);
    bytes[4] = uint8_t(types.size()); bytes[5] = uint8_t(windows); bytes[6] = 2;
    bytes[7] = uint8_t(regions.size()); bytes[8] = left_wins;
    for (unsigned i = 0; i < windows; ++i)
        std::snprintf(reinterpret_cast<char *>(bytes.data() + 12 + i * 32),32,"window-%u",i);
    cntrl::PageLayout layout{bytes.data()};
    for (unsigned i = 0; i < types.size(); ++i) {
        auto *p = bytes.data() + (layout.record(i) - bytes.data());
        std::snprintf(reinterpret_cast<char *>(p),32,"page-%u",i);
        p[32] = uint8_t(types[i]); p[33] = types[i] == 10 ? 0 : 1;
        for (unsigned w = 0; w < windows; ++w) p[34 + w] = uint8_t(windows - w - 1);
    }
    unsigned assigned = 0;
    for (unsigned r = 0; r < regions.size(); ++r) {
        auto *p = bytes.data() + (layout.region(r) - bytes.data());
        std::snprintf(reinterpret_cast<char *>(p),32,"region-%u",r);
        p[32] = uint8_t(assigned); p[33] = uint8_t(regions[r].size());
        for (unsigned page : regions[r]) bytes[layout.region(layout.regions()) - bytes.data() + assigned++] = uint8_t(page);
    }
    return bytes;
}
