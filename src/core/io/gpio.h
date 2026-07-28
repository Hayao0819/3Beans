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

// The five banks at 0x10147000. Only banks 1 and 3 can interrupt, and only
// bank 3's lines have a GIC interrupt each, starting at 0x48 for line 0.
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

    // Idle high, since the lines the 3DS wires up are active low
    uint16_t data[GPIO_BANKS] = { 0xFFFF, 0x00FF, 0xFFFF, 0xFFFF, 0xFFFF };
    uint16_t dir[GPIO_BANKS] = {};
    uint16_t edge[GPIO_BANKS] = {};
    uint16_t enable[GPIO_BANKS] = {};
    uint16_t pending[GPIO_BANKS] = {};

    void updateIrqs(int i);
};
