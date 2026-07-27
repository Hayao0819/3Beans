/*
    Copyright 2023-2026 Hydr8gon

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

#pragma once

#include <cstdint>
#include "../defines.h"

class Core;

// Access info packed into the free low bits of MmuMap::addr, which is page-aligned
enum MmuFlag {
    MMU_PR = (1 << 0), // privileged read (and execute unless MMU_XN)
    MMU_PW = (1 << 1), // privileged write
    MMU_UR = (1 << 2), // user read (and execute unless MMU_XN)
    MMU_UW = (1 << 3), // user write
    MMU_XN = (1 << 4), // execute never
    MMU_PAGE = (1 << 5), // second-level (page) entry rather than a section
    MMU_VALID = (1 << 6), // translation succeeded
    MMU_DOMAIN = 8 // 4-bit domain number at bits 11-8
};

struct MmuMap {
    uint8_t *read, *write;
    uint32_t *memTag;
    uint32_t addr, tag;
};

struct TcmMap {
    uint8_t *read, *write;
    uint32_t *memTag;
};

class Cp15 {
public:
    uint32_t exceptAddrs[MAX_CPUS] = {};

    Cp15(Core &core): core(core) {}
    uint8_t *getReadPtr(CpuId id, uint32_t address);

    void mmuInvalidate(CpuId id);
    void updateMap9(uint32_t start, uint32_t end);

    template <typename T> T read(CpuId id, uint32_t address);
    template <typename T> void write(CpuId id, uint32_t address, T value);

    uint32_t readReg(CpuId id, uint8_t cn, uint8_t cm, uint8_t cp);
    void writeReg(CpuId id, uint8_t cn, uint8_t cm, uint8_t cp, uint32_t value);

    bool fetchWouldFault(CpuId id, uint32_t address);
    void raiseFetchFault(CpuId id, uint32_t address);
    void setFault(CpuId id, const struct CpuFault &fault);

private:
    Core &core;

    uint32_t dfsr[MAX_CPUS - 1] = {}, ifsr[MAX_CPUS - 1] = {};
    uint32_t dfar[MAX_CPUS - 1] = {}, ifar[MAX_CPUS - 1] = {};
    uint32_t dacRegs[MAX_CPUS - 1] = {};
    uint32_t nullTag = 0; // stands in for memTag on entries with no backing memory

    MmuMap mmuMaps[MAX_CPUS - 1][0x100000] = {};
    TcmMap tcmMap[0x100000] = {};

    uint32_t mmuTags[MAX_CPUS - 1] = { 1, 1, 1, 1 };
    bool mmuEnables[MAX_CPUS - 1] = {};
    uint8_t itcm[0x8000] = {};
    uint8_t dtcm[0x4000] = {};

    bool dtcmRead = false, dtcmWrite = false;
    bool itcmRead = false, itcmWrite = false;
    uint32_t dtcmAddr = 0, dtcmSize = 0;
    uint32_t itcmSize = 0;

    uint32_t ctrlRegs[MAX_CPUS] = { 0x54078, 0x54078, 0x78 };
    uint32_t tlbBase0Regs[MAX_CPUS - 1] = {};
    uint32_t tlbBase1Regs[MAX_CPUS - 1] = {};
    uint32_t tlbCtrlRegs[MAX_CPUS - 1] = {};
    uint32_t physAddrRegs[MAX_CPUS - 1] = {};
    uint32_t threadIdRegs[MAX_CPUS - 1][3] = {};
    uint32_t dtcmReg = 0;
    uint32_t itcmReg = 0;

    uint32_t mmuTranslate(CpuId id, uint32_t address);
    uint32_t mmuWalk(CpuId id, uint32_t address, uint32_t &flags);
    void updateEntry(CpuId id, uint32_t address);
    bool checkAccess(CpuId id, uint32_t flags, uint32_t need);
    void throwFault(CpuId id, uint32_t address, uint32_t flags, bool write, bool prefetch);

    void writeCtrl11(CpuId id, uint32_t value);
    void writeCtrl9(CpuId id, uint32_t value);
    void writeTlbBase0(CpuId id, uint32_t value);
    void writeTlbBase1(CpuId id, uint32_t value);
    void writeTlbCtrl(CpuId id, uint32_t value);
    void writeAddrTrans(CpuId id, uint32_t value);
    void writeThreadId(CpuId id, int i, uint32_t value);
    void writeWfi(CpuId id, uint32_t value);
    void writeDtcm(CpuId id, uint32_t value);
    void writeItcm(CpuId id, uint32_t value);
};
