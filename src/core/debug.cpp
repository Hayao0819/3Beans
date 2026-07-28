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

#include <cstdio>
#include <cstring>

#include "debug.h"

namespace Debug {

const char *cpuName(int id) {
    static const char *names[MAX_CPUS] = { "ARM11A", "ARM11B", "ARM11C", "ARM11D", "ARM9" };
    return (id >= 0 && id < MAX_CPUS) ? names[id] : "?";
}

const char *keyName(int i) {
    static const char *names[] = { "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L", "X", "Y" };
    return (i >= 0 && i < 12) ? names[i] : "?";
}

int keyIndex(const std::string &name) {
    for (int i = 0; i < 12; i++)
        if (name == keyName(i)) return i;
    return -1;
}

bool safePhys(uint32_t addr, uint32_t len) {
    struct { uint32_t start, end; } ranges[] = {
        { 0x08000000, 0x08180000 }, // ARM9 internal RAM
        { 0x18000000, 0x18600000 }, // VRAM
        { 0x1FF00000, 0x20000000 }, // DSP/AXI WRAM
        { 0x20000000, 0x30000000 }, // FCRAM (+N3DS extension)
    };
    for (auto &r : ranges)
        if (addr >= r.start && len <= r.end - addr) return true;
    return false;
}

bool virtToPhys(Core *core, int cpu, uint32_t vaddr, uint32_t &paddr) {
    if (cpu < 0 || cpu >= ARM9) return false; // ARM9 has no MMU
    uint32_t ttbcr = core->cp15.readReg((CpuId)cpu, 2, 0, 2) & 7;
    uint32_t ttbr = core->cp15.readReg((CpuId)cpu, 2, 0, 0);
    if (ttbcr && (vaddr >> (32 - ttbcr)))
        ttbr = core->cp15.readReg((CpuId)cpu, 2, 0, 1);
    uint32_t l1Addr = (ttbr & ~0x3FFF) | (((vaddr >> 20) & 0xFFF) << 2);
    if (!safePhys(l1Addr, 4)) return false;
    uint32_t l1 = core->memory.read<uint32_t>((CpuId)cpu, l1Addr);
    switch (l1 & 3) {
        case 2: // Section or supersection
            if (l1 & BIT(18))
                paddr = (l1 & 0xFF000000) | (vaddr & 0xFFFFFF);
            else
                paddr = (l1 & 0xFFF00000) | (vaddr & 0xFFFFF);
            return true;
        case 1: { // Coarse page table
            uint32_t l2Addr = (l1 & ~0x3FF) | (((vaddr >> 12) & 0xFF) << 2);
            if (!safePhys(l2Addr, 4)) return false;
            uint32_t l2 = core->memory.read<uint32_t>((CpuId)cpu, l2Addr);
            if ((l2 & 3) == 1) { // 64KB large page
                paddr = (l2 & 0xFFFF0000) | (vaddr & 0xFFFF);
                return true;
            }
            if (l2 & 2) { // 4KB small page
                paddr = (l2 & 0xFFFFF000) | (vaddr & 0xFFF);
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

std::string hexDump(Core *core, int cpu, uint32_t addr, uint32_t len, bool virt, std::string &error) {
    std::string out;
    char buf[16];
    for (uint32_t i = 0; i < len; i += 16) {
        snprintf(buf, sizeof(buf), "%08x: ", addr + i);
        out += buf;
        std::string ascii;
        for (uint32_t k = 0; k < 16 && i + k < len; k++) {
            uint32_t a = addr + i + k, pa = a;
            if (virt && !virtToPhys(core, cpu, a, pa)) {
                error = "unmapped virtual address";
                return out;
            }
            if (!safePhys(pa, 1)) {
                error = "address outside RAM (use unsafe for MMIO)";
                return out;
            }
            uint8_t b = core->memory.read<uint8_t>((CpuId)cpu, pa);
            snprintf(buf, sizeof(buf), "%02x ", b);
            out += buf;
            ascii += (b >= 0x20 && b < 0x7F) ? (char)b : '.';
        }
        out += ' ' + ascii + '\n';
    }
    return out;
}

std::string rawDump(Core *core, int cpu, uint32_t addr, uint32_t len) {
    std::string out;
    char buf[16];
    for (uint32_t i = 0; i < len; i += 4) {
        if (i % 16 == 0) {
            snprintf(buf, sizeof(buf), "%s%08x: ", i ? "\n" : "", addr + i);
            out += buf;
        }
        snprintf(buf, sizeof(buf), "%08x ", core->memory.read<uint32_t>((CpuId)cpu, addr + i));
        out += buf;
    }
    return out + "\n";
}

CpuState cpuState(Core *core, int cpu) {
    CpuState s;
    if (cpu < 0 || cpu >= MAX_CPUS || !core->arms[cpu].registers[0]) return s;
    for (int i = 0; i < 16; i++)
        s.regs[i] = *core->arms[cpu].registers[i];
    s.cpsr = core->arms[cpu].cpsr;
    s.halted = core->arms[cpu].halted;
    s.valid = true;
    return s;
}

std::vector<Cp15Reg> cp15Regs(Core *core, int cpu) {
    static const struct { const char *name; uint8_t cn, cm, cp; } regs[] = {
        { "SCTLR", 1, 0, 0 }, { "TTBR0", 2, 0, 0 }, { "TTBR1", 2, 0, 1 }, { "TTBCR", 2, 0, 2 },
        { "DACR", 3, 0, 0 }, { "DFSR", 5, 0, 0 }, { "IFSR", 5, 0, 1 }, { "DFAR", 6, 0, 0 },
        { "IFAR", 6, 0, 2 }
    };
    std::vector<Cp15Reg> out;
    if (cpu < 0 || cpu >= ARM9) return out;
    for (const auto &r : regs)
        out.push_back({ r.name, core->cp15.readReg((CpuId)cpu, r.cn, r.cm, r.cp) });
    return out;
}

std::string mcuState(Core *core) {
    // The low bits are the buttons the guest's interrupt controller maps; the rest are
    // LCD power events that nothing in a Linux device tree claims
    static const char *names[6] = { "power press", "power release", "home press",
        "home release", "wifi press", "wifi release" };
    uint32_t flags = core->i2c.getMcuIrqFlags(), mask = core->i2c.getMcuIrqMask();
    char buf[128];
    snprintf(buf, sizeof(buf), "MCU IRQ flags = %08x\nMCU IRQ mask  = %08x\n\n", flags, mask);
    std::string out = buf;
    for (int i = 0; i < 6; i++) {
        snprintf(buf, sizeof(buf), "  bit %d  %-14s %s%s\n", i, names[i],
            (flags & BIT(i)) ? "pending" : "-", (mask & BIT(i)) ? "  (masked)" : "");
        out += buf;
    }
    return out;
}

std::string gpioState(Core *core) {
    // A line is asserted while its level matches the edge bit, which is what the guest's
    // interrupt handler looks for; bank 3 line 9 is the MCU and line 1 the IR UART
    std::string out = "bank  data  dir   edge  irqen  asserted\n";
    char buf[96];
    for (int i = 0; i < GPIO_BANKS; i++) {
        uint16_t data = core->gpio.readData(i), edge = core->gpio.readIrqEdge(i);
        uint16_t enable = core->gpio.readIrqEnable(i);
        snprintf(buf, sizeof(buf), "%-5d %04x  %04x  %04x  %04x   %04x\n", i, data,
            core->gpio.readDir(i), edge, enable, (uint16_t)(~(data ^ edge) & enable));
        out += buf;
    }
    return out;
}

std::string faultList(Core *core, int limit, bool first) {
    uint32_t total = core->faultCount.load();
    if (!total) return "no faults recorded\n";
    uint32_t stored = total < Core::MAX_FAULTS ? total : Core::MAX_FAULTS;
    int show = (int)stored < limit ? (int)stored : limit;
    std::string out = "total=" + std::to_string(total) +
        (first ? " showing first " : " showing last ") + std::to_string(show) + ":\n";
    static const char *kinds[] = { "unkARM", "unkTHUMB", "dataAbort", "prefAbort" };
    static const char *statuses[16] = { "", "align", "", "align", "", "translation-section", "",
        "translation-page", "", "domain-section", "", "domain-page", "", "permission-section", "",
        "permission-page" };
    char buf[128];
    for (int i = 0; i < show; i++) {
        Core::Fault f = first ? core->firstFaults[i] : core->faults[(total - 1 - i) % Core::MAX_FAULTS];
        snprintf(buf, sizeof(buf), "%-9s cpu%d pc=%08x addr=%08x %s\n", f.kind < 4 ? kinds[f.kind] : "?",
            f.cpu, f.pc, f.addr, statuses[f.status & 0xF]);
        out += buf;
    }
    return out;
}

void traceArm() {
    Core::traceIdx = 0;
    Core::traceFrozen = false;
    Core::traceOn = true;
}

std::string traceList(int limit) {
    uint32_t total = Core::traceIdx;
    if (!total) return "trace empty (arm it before booting)\n";
    uint32_t stored = total < (uint32_t)Core::TRACE_N ? total : (uint32_t)Core::TRACE_N;
    int show = (int)stored < limit ? (int)stored : limit;
    std::string out = std::string(Core::traceFrozen ? "FROZEN (user-mode zero opcode)" : "live") +
        ", recorded=" + std::to_string(total) + ", showing last " + std::to_string(show) + ":\n";
    char buf[64];
    uint32_t prevCpsr = ~0u, prevCore = ~0u;
    for (int i = show - 1; i >= 0; i--) {
        uint32_t slot = ((total - 1 - i) & (Core::TRACE_N - 1)) * 3;
        uint32_t pc = Core::traceBuf[slot], cp = Core::traceBuf[slot + 1], cr = Core::traceBuf[slot + 2];
        // Annotate core/mode only when they change, to keep the dump compact
        if (cp != prevCpsr || cr != prevCore) {
            snprintf(buf, sizeof(buf), "%08x  core%u cpsr=%08x mode=%02x %s\n",
                pc, cr, cp, cp & 0x1F, (cp & BIT(5)) ? "THUMB" : "ARM");
            prevCpsr = cp;
            prevCore = cr;
        }
        else snprintf(buf, sizeof(buf), "%08x\n", pc);
        out += buf;
    }
    return out;
}

std::vector<uint32_t> findPattern(Core *core, const std::vector<uint8_t> &pattern,
        uint32_t start, uint32_t end, int max) {
    std::vector<uint32_t> hits;
    if (pattern.empty()) return hits;
    std::vector<uint8_t> buf(0x1000 + pattern.size());
    for (uint64_t base = start; base < end && (int)hits.size() < max; base += 0x1000) {
        if (!safePhys((uint32_t)base, 1)) continue;
        uint32_t len = 0x1000 + ((base + 0x1000 + pattern.size() <= end) ? pattern.size() : 0);
        for (uint32_t i = 0; i < len; i++)
            buf[i] = core->memory.read<uint8_t>(ARM11A, (uint32_t)base + i);
        for (uint32_t i = 0; i + pattern.size() <= len && (int)hits.size() < max; i++)
            if (!memcmp(&buf[i], pattern.data(), pattern.size()))
                hits.push_back((uint32_t)(base + i));
    }
    return hits;
}

}
