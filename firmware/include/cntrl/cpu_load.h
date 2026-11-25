#pragma once
#include <array>
#include <cstdint>

namespace cntrl {
enum class CpuWork : unsigned { midi, display, input, storage, count };

// Activity on the foreground loop. Retain ten completed 100 ms samples.
template<uint32_t (*ReadCycles)()>
class CpuLoad {
    static constexpr unsigned categories = unsigned(CpuWork::count);
    static constexpr unsigned samples = 10;

public:
    struct Reading {
        std::array<unsigned, categories> basis_points{};
        unsigned total_basis_points = 0;
        uint32_t window_ms = 0;
        uint32_t max_scan_gap_ms = 0;

        unsigned work(CpuWork kind) const { return basis_points[unsigned(kind)]; }
    };

    class Scope {
    public:
        Scope(CpuLoad &load, CpuWork kind)
            : load_(load), parent_(load.scope_), kind_(kind), started_(ReadCycles()) {
            load_.scope_ = this;
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;
        ~Scope() {
            const uint32_t elapsed = ReadCycles() - started_;
            load_.active_.work[unsigned(kind_)] += elapsed - children_;
            if (parent_) parent_->children_ += elapsed;
            load_.scope_ = parent_;
        }

    private:
        CpuLoad &load_;
        Scope *parent_;
        CpuWork kind_;
        uint32_t started_, children_ = 0;
    };

    void start(uint32_t now) {
        active_ = {};
        previous_ms_ = now;
        previous_cycles_ = ReadCycles();
    }

    void sample(uint32_t now) {
        const uint32_t interval = now - previous_ms_;
        if (interval < 100) return;
        const uint32_t cycles = ReadCycles();
        active_.elapsed = cycles - previous_cycles_;
        active_.milliseconds = interval;
        history_[next_] = active_;
        next_ = (next_ + 1) % samples;
        active_ = {};
        previous_cycles_ = cycles;
        previous_ms_ = now;
    }

    void scanned(uint32_t now) {
        const uint32_t gap = scanned_ ? now - previous_scan_ms_ : 0;
        previous_scan_ms_ = now;
        scanned_ = true;
        if (gap > active_.max_scan_gap) active_.max_scan_gap = gap;
    }

    Reading reading() const {
        Reading result;
        uint64_t elapsed = 0, total = 0;
        std::array<uint64_t, categories> work{};
        for (const auto &sample : history_) {
            elapsed += sample.elapsed;
            result.window_ms += sample.milliseconds;
            for (unsigned i = 0; i < categories; ++i) work[i] += sample.work[i];
            if (sample.max_scan_gap > result.max_scan_gap_ms)
                result.max_scan_gap_ms = sample.max_scan_gap;
        }
        if (!elapsed) return result;
        for (unsigned i = 0; i < categories; ++i) {
            total += work[i];
            result.basis_points[i] = unsigned(work[i] * 10000 / elapsed);
        }
        result.total_basis_points = unsigned(total * 10000 / elapsed);
        return result;
    }

private:
    struct Sample {
        std::array<uint32_t, categories> work{};
        uint32_t elapsed = 0, milliseconds = 0, max_scan_gap = 0;
    };

    std::array<Sample, samples> history_{};
    Sample active_{};
    Scope *scope_ = nullptr;
    uint32_t previous_ms_ = 0, previous_cycles_ = 0;
    uint32_t previous_scan_ms_ = 0;
    unsigned next_ = 0;
    bool scanned_ = false;
};
} // namespace cntrl
