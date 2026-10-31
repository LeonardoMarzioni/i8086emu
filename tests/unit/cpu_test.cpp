#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/cpu.hpp"
#include "test_machine.hpp"

using namespace emu;
using emu::test::TestMachine;

namespace {

using Status = StepResult::Status;

}  // namespace

TEST_CASE("MOV registro, immediato e poi HLT", "[cpu]") {
    TestMachine m({
        0xB8, 0x34, 0x12,  // MOV AX, 1234h
        0xB3, 0x56,        // MOV BL, 56h
        0xF4,              // HLT
    });
    CHECK(m.step().status == Status::Executed);
    CHECK(m.regs().get(Reg16::AX) == 0x1234);
    CHECK(m.regs().ip == 0x0103);

    m.step();
    CHECK(m.regs().get(Reg8::BL) == 0x56);

    const StepResult hlt = m.step();
    CHECK(hlt.status == Status::Halted);
    CHECK(hlt.ip == 0x0105);       // indirizzo di HLT
    CHECK(m.regs().ip == 0x0106);  // IP già oltre HLT
    CHECK(m.cpu.halted());
}

TEST_CASE("dopo HLT la CPU resta ferma fino al reset", "[cpu]") {
    TestMachine m({0xF4, 0x40});  // HLT; INC AX
    m.step();
    const Registers before = m.regs();
    CHECK(m.step().status == Status::Halted);
    CHECK(m.regs() == before);  // INC AX non è stata eseguita

    m.cpu.reset();
    CHECK_FALSE(m.cpu.halted());
    CHECK(m.regs().ip == 0);
    CHECK(m.regs().flags.word() == 0xF002);
}

TEST_CASE("MOV tra registri nelle quattro forme 88-8B", "[cpu]") {
    TestMachine m({
        0x88, 0xC4,  // MOV AH, AL
        0x8A, 0xF8,  // MOV BH, AL
        0x89, 0xC3,  // MOV BX, AX
        0x8B, 0xCB,  // MOV CX, BX
    });
    m.regs().set(Reg8::AL, 0x5A);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x5A5A);
    m.step();
    CHECK(m.regs().get(Reg8::BH) == 0x5A);
    m.step();
    CHECK(m.regs().get(Reg16::BX) == 0x5A5A);
    m.step();
    CHECK(m.regs().get(Reg16::CX) == 0x5A5A);
    CHECK(m.regs().ip == 0x0108);
}

TEST_CASE("MOV non modifica i flag", "[cpu]") {
    TestMachine m({0xB8, 0x00, 0x00});  // MOV AX, 0
    m.regs().set(Reg16::AX, 0x1234);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0);
    CHECK(m.regs().flags.word() == 0xF002);  // ZF resta a 0
}

TEST_CASE("MOV r/m, immediato con C6/C7", "[cpu]") {
    TestMachine m({
        0xC6, 0xC0, 0x42,        // MOV AL, 42h
        0xC7, 0xC3, 0x34, 0x12,  // MOV BX, 1234h
    });
    m.step();
    CHECK(m.regs().get(Reg8::AL) == 0x42);
    m.step();
    CHECK(m.regs().get(Reg16::BX) == 0x1234);
}

TEST_CASE("ALU accumulatore-immediato", "[cpu]") {
    TestMachine m({
        0x04, 0xFF,        // ADD AL, FFh
        0x2D, 0x01, 0x00,  // SUB AX, 1
    });
    m.regs().set(Reg16::AX, 0x1201);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x1200);  // AH intatto
    CHECK(m.regs().flags.test(Flag::CF));
    CHECK(m.regs().flags.test(Flag::ZF));

    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x11FF);
    CHECK_FALSE(m.regs().flags.test(Flag::CF));
}

TEST_CASE("ALU tra registri", "[cpu]") {
    TestMachine m({
        0x01, 0xD8,  // ADD AX, BX
        0x29, 0xD8,  // SUB AX, BX
        0x30, 0xE4,  // XOR AH, AH
    });
    m.regs().set(Reg16::AX, 0x7FFF);
    m.regs().set(Reg16::BX, 0x0001);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x8000);
    CHECK(m.regs().flags.test(Flag::OF));
    CHECK(m.regs().flags.test(Flag::SF));

    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x7FFF);
    CHECK(m.regs().flags.test(Flag::OF));  // 8000h - 1: overflow da negativo a positivo

    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x00FF);
    CHECK(m.regs().flags.test(Flag::ZF));
    CHECK_FALSE(m.regs().flags.test(Flag::OF));
    CHECK_FALSE(m.regs().flags.test(Flag::CF));
}

TEST_CASE("CMP aggiorna solo i flag", "[cpu]") {
    TestMachine m({
        0x39, 0xD8,  // CMP AX, BX
        0x3C, 0x05,  // CMP AL, 5
    });
    m.regs().set(Reg16::AX, 0x1203);
    m.regs().set(Reg16::BX, 0x1203);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x1203);
    CHECK(m.regs().flags.test(Flag::ZF));

    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x1203);
    CHECK(m.regs().flags.test(Flag::CF));  // 3 < 5
    CHECK_FALSE(m.regs().flags.test(Flag::ZF));
}

TEST_CASE("gruppo 1 con immediati a 8, 16 e 8 bit esteso", "[cpu]") {
    TestMachine m({
        0x80, 0xC3, 0x10,        // ADD BL, 10h
        0x81, 0xEB, 0x00, 0x01,  // SUB BX, 0100h
        0x83, 0xC0, 0xFF,        // ADD AX, -1
        0x83, 0xE1, 0x0F,        // AND CX, 000Fh
        0x83, 0xFA, 0x80,        // CMP DX, -128
    });
    m.regs().set(Reg16::BX, 0x02F8);
    m.regs().set(Reg16::AX, 0x0001);
    m.regs().set(Reg16::CX, 0x1234);
    m.regs().set(Reg16::DX, 0xFF80);

    m.step();
    CHECK(m.regs().get(Reg16::BX) == 0x0208);  // F8h + 10h: riporto perso, BH intatto
    CHECK(m.regs().flags.test(Flag::CF));
    m.step();
    CHECK(m.regs().get(Reg16::BX) == 0x0108);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x0000);
    CHECK(m.regs().flags.test(Flag::CF));
    CHECK(m.regs().flags.test(Flag::ZF));
    m.step();
    CHECK(m.regs().get(Reg16::CX) == 0x0004);
    m.step();
    CHECK(m.regs().get(Reg16::DX) == 0xFF80);
    CHECK(m.regs().flags.test(Flag::ZF));
}

TEST_CASE("INC e DEC non toccano CF", "[cpu]") {
    TestMachine m({
        0x40,        // INC AX
        0x4B,        // DEC BX
        0xFE, 0xC4,  // INC AH
        0xFF, 0xC9,  // DEC CX
    });
    m.regs().set(Reg16::AX, 0xFFFF);
    m.regs().flags.set(Flag::CF, true);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x0000);
    CHECK(m.regs().flags.test(Flag::ZF));
    CHECK(m.regs().flags.test(Flag::CF));  // invariato

    m.regs().flags.set(Flag::CF, false);
    m.step();
    CHECK(m.regs().get(Reg16::BX) == 0xFFFF);
    CHECK_FALSE(m.regs().flags.test(Flag::CF));  // invariato anche con prestito

    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x0100);
    m.step();
    CHECK(m.regs().get(Reg16::CX) == 0xFFFF);
}

TEST_CASE("NEG e NOT", "[cpu]") {
    TestMachine m({
        0xF6, 0xD8,  // NEG AL
        0xF7, 0xD3,  // NOT BX
    });
    m.regs().set(Reg16::AX, 0x0001);
    m.regs().set(Reg16::BX, 0x00FF);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x00FF);
    CHECK(m.regs().flags.test(Flag::CF));

    const std::uint16_t flags_before = m.regs().flags.word();
    m.step();
    CHECK(m.regs().get(Reg16::BX) == 0xFF00);
    CHECK(m.regs().flags.word() == flags_before);
}

TEST_CASE("NOP fa avanzare solo IP", "[cpu]") {
    TestMachine m({0x90});
    const Registers before = m.regs();
    CHECK(m.step().status == Status::Executed);
    Registers expected = before;
    expected.ip = 0x0101;
    CHECK(m.regs() == expected);
}

TEST_CASE("un opcode non implementato non modifica lo stato", "[cpu]") {
    TestMachine m({0x10, 0xC0});  // ADC AL, AL
    m.regs().set(Reg16::AX, 0x1234);
    const Registers before = m.regs();
    const StepResult r = m.step();
    CHECK(r.status == Status::Unimplemented);
    CHECK(r.opcode == 0x10);
    CHECK(r.cs == 0x1000);
    CHECK(r.ip == 0x0100);
    CHECK_FALSE(r.group_reg.has_value());
    CHECK(m.regs() == before);
}

TEST_CASE("in un gruppo si riporta anche il campo reg", "[cpu]") {
    TestMachine m({0x80, 0xD0, 0x05});  // ADC AL, 5
    const Registers before = m.regs();
    const StepResult r = m.step();
    CHECK(r.status == Status::Unimplemented);
    CHECK(r.opcode == 0x80);
    CHECK(r.group_reg == 2);
    CHECK(m.regs() == before);
}

TEST_CASE("IP fa wrap a 16 bit dentro CS", "[cpu]") {
    TestMachine m(std::vector<std::uint8_t>{});
    m.memory.write8(0x1000, 0xFFFF, 0xB0);  // MOV AL, 42h a cavallo della fine del segmento
    m.memory.write8(0x1000, 0x0000, 0x42);
    m.regs().ip = 0xFFFF;
    m.step();
    CHECK(m.regs().get(Reg8::AL) == 0x42);
    CHECK(m.regs().ip == 0x0001);
}
