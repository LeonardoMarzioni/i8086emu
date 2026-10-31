#pragma once

#include <cstdint>
#include <vector>

#include "core/cpu.hpp"

namespace emu::test {

// Memoria e CPU con il codice caricato a CS:IP = 1000:0100.
struct TestMachine {
    static constexpr std::uint16_t kCodeSegment = 0x1000;
    static constexpr std::uint16_t kCodeOffset = 0x0100;

    Memory memory;
    Cpu cpu{memory};

    explicit TestMachine(const std::vector<std::uint8_t>& code) {
        memory.load(physical_address(kCodeSegment, kCodeOffset), code);
        cpu.regs().set(SegReg::CS, kCodeSegment);
        cpu.regs().ip = kCodeOffset;
    }

    Registers& regs() { return cpu.regs(); }
    StepResult step() { return cpu.step(); }
};

}  // namespace emu::test
