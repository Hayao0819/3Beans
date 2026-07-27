/*
    Copyright 2026 Hayao0819

    This file is part of 3Beans.

    3Beans is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    3Beans is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with 3Beans. If not, see <https://www.gnu.org/licenses/>.
*/

// debug.h - inspection of a running core, with no dependency on how it is presented.
// The MCP server (desktop/debugger.h) and the debugger window (desktop/debug_dialog.h)
// are both thin wrappers around this.

#pragma once

#include <string>
#include <vector>

#include "core.h"

namespace Debug {
    const char *cpuName(int id);
    const char *keyName(int i);
    int keyIndex(const std::string &name);

    // Physical RAM ranges that are safe to read without MMIO side effects
    bool safePhys(uint32_t addr, uint32_t len);

    // ARMv6 short-descriptor table walk using physical reads, so it costs the guest nothing
    bool virtToPhys(Core *core, int cpu, uint32_t vaddr, uint32_t &paddr);

    // "address: 16 hex bytes  ascii" lines, stopping at the first address it cannot reach
    std::string hexDump(Core *core, int cpu, uint32_t addr, uint32_t len, bool virt, std::string &error);

    // Word dump straight through the memory bus, MMIO side effects included
    std::string rawDump(Core *core, int cpu, uint32_t addr, uint32_t len);

    struct CpuState {
        bool valid = false;
        uint32_t regs[16] = {};
        uint32_t cpsr = 0;
        uint8_t halted = 0;
    };
    CpuState cpuState(Core *core, int cpu);

    struct Cp15Reg { const char *name; uint32_t value; };
    std::vector<Cp15Reg> cp15Regs(Core *core, int cpu);

    std::string faultList(Core *core, int limit, bool first);
    std::string traceList(int limit);
    void traceArm();

    std::vector<uint32_t> findPattern(Core *core, const std::vector<uint8_t> &pattern,
        uint32_t start, uint32_t end, int max);
}
