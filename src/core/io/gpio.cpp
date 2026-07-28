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

#include "../core.h"

void Gpio::writeData(int i, uint16_t mask, uint16_t value) {
    // Write to a bank's data register, leaving input lines alone
    mask &= dir[i];
    data[i] = (data[i] & ~mask) | (value & mask);
    updateIrqs(i);
}

void Gpio::writeDir(int i, uint16_t mask, uint16_t value) {
    // Write to a bank's direction register
    dir[i] = (dir[i] & ~mask) | (value & mask);
}

void Gpio::writeIrqEdge(int i, uint16_t mask, uint16_t value) {
    // Write to a bank's interrupt edge register, which selects the level to trigger on
    edge[i] = (edge[i] & ~mask) | (value & mask);
    updateIrqs(i);
}

void Gpio::writeIrqEnable(int i, uint16_t mask, uint16_t value) {
    // Write to a bank's interrupt enable register
    enable[i] = (enable[i] & ~mask) | (value & mask);
    updateIrqs(i);
}

void Gpio::setLine(int i, int line, bool high) {
    // Drive an input line from whatever peripheral owns it
    uint16_t bit = BIT(line);
    if (high)
        data[i] |= bit;
    else
        data[i] &= ~bit;
    updateIrqs(i);
}

void Gpio::updateIrqs(int i) {
    // A line is asserted while its level matches the edge bit the guest wrote, and the
    // GIC latches on the transition into that state, one interrupt per line
    uint16_t state = ~(data[i] ^ edge[i]) & enable[i];
    uint16_t rising = state & ~pending[i];
    pending[i] = state;
    if (i == GPIO_BANK1) {
        if (rising & BIT(0)) core.interrupts.sendInterrupt(ARM11, 0x64);
        if (rising & BIT(1)) core.interrupts.sendInterrupt(ARM11, 0x66);
    }
    else if (i == GPIO_BANK3) {
        for (int j = 0; j < 12; j++)
            if (rising & BIT(j))
                core.interrupts.sendInterrupt(ARM11, 0x68 + j);
    }
}
