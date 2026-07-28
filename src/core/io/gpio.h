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

#pragma once

#include <cstdint>

class Core;

// The five banks at 0x10147000, named as the device tree orders them. Only banks 1
// and 3 can interrupt, and each of their lines has its own GIC interrupt.
enum GpioBank {
    GPIO_BANK0 = 0,
    GPIO_BANK1,
    GPIO_BANK2,
    GPIO_BANK3,
    GPIO_BANK4,
    GPIO_BANKS
};

class Gpio {
public:
    Gpio(Core &core): core(core) {}

    uint16_t readData(int i) { return data[i]; }
    uint16_t readDir(int i) { return dir[i]; }
    uint16_t readIrqEdge(int i) { return edge[i]; }
    uint16_t readIrqEnable(int i) { return enable[i]; }

    void writeData(int i, uint16_t mask, uint16_t value);
    void writeDir(int i, uint16_t mask, uint16_t value);
    void writeIrqEdge(int i, uint16_t mask, uint16_t value);
    void writeIrqEnable(int i, uint16_t mask, uint16_t value);

    void setLine(int i, int line, bool high);

private:
    Core &core;

    // Power-on values per 3dbrew, except that bank 3 bit 9 starts released: that is
    // the MCU's own line and the dump it was taken from had an interrupt pending
    uint16_t data[GPIO_BANKS] = { 0x0003, 0x0002, 0x0000, 0x0FFB, 0x0000 };
    uint16_t dir[GPIO_BANKS] = {};
    uint16_t edge[GPIO_BANKS] = {};
    uint16_t enable[GPIO_BANKS] = {};
    uint16_t pending[GPIO_BANKS] = {};

    void updateIrqs(int i);
};
