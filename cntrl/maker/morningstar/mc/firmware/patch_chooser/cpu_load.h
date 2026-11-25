#pragma once
#include <Arduino.h>
#include <cntrl/cpu_load.h>

namespace cntrl::device {
inline uint32_t cpu_cycles() { return ARM_DWT_CYCCNT; }
inline CpuLoad<cpu_cycles> cpu_load;
using CpuWorkScope = CpuLoad<cpu_cycles>::Scope;
} // namespace cntrl::device
