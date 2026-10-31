#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/loader.hpp"

using namespace emu;

TEST_CASE("caricamento in stile .COM con i valori predefiniti", "[loader]") {
    Memory mem;
    Cpu cpu(mem);
    mem.poke(0x50000, 0xAA);  // deve sparire: la memoria viene azzerata
    cpu.regs().set(Reg16::AX, 0x1234);

    const std::vector<std::uint8_t> program{0xB8, 0x34, 0x12, 0xF4};
    REQUIRE(load_program(mem, cpu, program));

    const Registers& r = cpu.regs();
    for (const SegReg s : {SegReg::ES, SegReg::CS, SegReg::SS, SegReg::DS}) {
        CHECK(r.get(s) == 0x1000);
    }
    CHECK(r.ip == 0x0100);
    CHECK(r.get(Reg16::SP) == 0xFFFE);
    CHECK(r.get(Reg16::AX) == 0);
    CHECK(r.flags.word() == 0xF002);
    CHECK(mem.read8(0x1000, 0x0100) == 0xB8);
    CHECK(mem.read8(0x1000, 0x0103) == 0xF4);
    CHECK(mem.peek(0x50000) == 0x00);
}

TEST_CASE("caricamento con valori configurati", "[loader]") {
    Memory mem;
    Cpu cpu(mem);
    const LoadConfig config{0x2000, 0x0000, 0x1000};
    REQUIRE(load_program(mem, cpu, std::vector<std::uint8_t>{0x90}, config));
    CHECK(cpu.regs().get(SegReg::CS) == 0x2000);
    CHECK(cpu.regs().ip == 0x0000);
    CHECK(cpu.regs().get(Reg16::SP) == 0x1000);
    CHECK(mem.peek(0x20000) == 0x90);
}

TEST_CASE("un programma che non entra nel segmento viene rifiutato", "[loader]") {
    Memory mem;
    Cpu cpu(mem);
    cpu.regs().set(Reg16::AX, 0x1234);
    const LoadConfig config;
    CHECK(max_program_size(config) == 0xFF00);

    const std::vector<std::uint8_t> fits(0xFF00, 0x90);
    CHECK(load_program(mem, cpu, fits, config));

    cpu.regs().set(Reg16::AX, 0x1234);
    const std::vector<std::uint8_t> too_big(0xFF01, 0x90);
    CHECK_FALSE(load_program(mem, cpu, too_big, config));
    CHECK(cpu.regs().get(Reg16::AX) == 0x1234);  // nulla è stato modificato
}
