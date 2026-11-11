#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace cntrl::midi {
struct ArpeggiatorSelection {
    unsigned kind = 0, flags = 0, modulus = 2;
    uint32_t up = 1, down = 1, denominator = 2, seed = 42;
};

class ArpeggiatorOrder {
public:
    void reset(ArpeggiatorSelection settings) {
        settings_ = settings; random_ = settings.seed; started_ = false;
        index_ = residue_ = phase_ = 0; direction_ = 1; shuffle_.clear();
    }
    void retain(unsigned rank) { index_ = rank; }
    unsigned next(unsigned size) {
        if (!size) return 0;
        index_ = std::min(index_, size - 1);
        const auto &s = settings_;
        if (s.kind == 4) return draw(size);
        if (s.kind == 7) {
            if (shuffle_.size() != size || phase_ == size) {
                shuffle_.resize(size); std::iota(shuffle_.begin(),shuffle_.end(),0);
                for (unsigned i = size; i > 1; --i) std::swap(shuffle_[i - 1],shuffle_[draw(i)]);
                phase_ = 0;
            }
            return shuffle_[phase_++];
        }
        if (s.kind == 8) { unsigned p = phase_++ % size; return p % 2 ? size - 1 - p / 2 : p / 2; }
        bool reverse = s.kind == 1 || (s.kind == 2 && (s.flags & 8));
        if (!started_) { started_ = true; index_ = reverse ? size - 1 : 0; return index_; }
        if (size == 1) return index_ = 0;
        if (s.kind == 5) {
            unsigned r = draw(s.denominator);
            int move = r < s.up ? 1 : r < uint64_t(s.up) + s.down ? -1 : 0;
            int next = int(index_) + move;
            if (next < 0) next = s.flags & 4 ? int(size) - 1 : 1;
            if (next >= int(size)) next = s.flags & 4 ? 0 : int(size) - 2;
            return index_ = unsigned(next);
        }
        if (s.kind == 6) {
            unsigned modulus = std::min(s.modulus,size);
            if (index_ + modulus < size) index_ += modulus;
            else { residue_ = (residue_ + 1) % modulus; index_ = residue_; }
            return index_;
        }
        if (s.kind == 3) {
            if (direction_ > 0 && index_ == size - 1) { direction_ = -1; if (s.flags & 2) return index_; }
            else if (direction_ < 0 && !index_) { direction_ = 1; if (s.flags & 1) return index_; }
            index_ = unsigned(int(index_) + direction_); return index_;
        }
        return index_ = reverse ? (index_ ? index_ - 1 : size - 1) : (index_ + 1) % size;
    }
private:
    ArpeggiatorSelection settings_;
    uint32_t random_ = 42;
    unsigned index_ = 0, residue_ = 0, phase_ = 0;
    int direction_ = 1;
    bool started_ = false;
    std::vector<unsigned> shuffle_;
    unsigned draw(unsigned bound) {
        uint32_t value, threshold = uint32_t(-bound) % bound;
        do { random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5; value = random_; } while (value < threshold);
        return value % bound;
    }
};
} // namespace cntrl::midi
