#include <cntrl/cpu_load.h>
#include <cassert>
#include <limits>

namespace {
uint32_t cycles;
uint32_t read_cycles() { return cycles; }
using Load = cntrl::CpuLoad<read_cycles>;
using cntrl::CpuWork;
}

int main() {
    Load load;
    cycles = std::numeric_limits<uint32_t>::max() - 99;
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 49;
    const uint32_t first_cycle = cycles;
    load.start(start);
    {
        const Load::Scope midi(load, CpuWork::midi);
        cycles += 200;
        {
            const Load::Scope storage(load, CpuWork::storage);
            cycles += 100;
        }
        cycles += 100;
    }
    {
        const Load::Scope display(load, CpuWork::display);
        cycles += 150;
    }
    {
        const Load::Scope input(load, CpuWork::input);
        cycles += 50;
    }
    load.scanned(start + 1);
    load.scanned(start + 6);
    for (unsigned i = 7; i <= 100; ++i) load.scanned(start + i);
    load.sample(start + 99);
    assert(load.reading().window_ms == 0);
    cycles = first_cycle + 1000;
    load.sample(start + 100);
    const auto first = load.reading();
    assert(first.window_ms == 100);
    assert(first.total_basis_points == 6000);
    assert(first.work(CpuWork::midi) == 3000);
    assert(first.work(CpuWork::storage) == 1000);
    assert(first.work(CpuWork::display) == 1500);
    assert(first.work(CpuWork::input) == 500);
    assert(first.max_scan_gap_ms == 5);

    for (unsigned i = 2; i <= 10; ++i) {
        cycles = first_cycle + i * 1000;
        for (unsigned j = 1; j <= 100; ++j) load.scanned(start + (i - 1) * 100 + j);
        load.sample(start + i * 100);
    }
    const auto full = load.reading();
    assert(full.window_ms == 1000);
    assert(full.total_basis_points == 600);
    assert(full.max_scan_gap_ms == 5);

    cycles += 1200;
    load.sample(start + 1120);
    const auto idle = load.reading();
    assert(idle.window_ms == 1020);
    assert(idle.total_basis_points == 0);
    assert(idle.max_scan_gap_ms == 1);
}
