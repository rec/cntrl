#pragma once
#include <cstdint>

struct PairSample {
    bool ready = false;
    char first = '=';
    uint32_t gap_ms = 0;
};

class PairTiming {
public:
    PairSample update(bool a, bool f, uint32_t now) {
        if (!a && !f) {
            first_ = 0;
            reported_ = false;
            return {};
        }
        if (reported_) return {};
        if (a && f) {
            reported_ = true;
            return {true, first_ ? first_ : '=', first_ ? uint32_t(now - since_) : 0};
        }
        char first = a ? 'A' : 'F';
        if (first != first_) {
            first_ = first;
            since_ = now;
        }
        return {};
    }
private:
    char first_ = 0;
    bool reported_ = false;
    uint32_t since_ = 0;
};
