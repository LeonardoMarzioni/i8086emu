#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/cpu.hpp"
#include "test_machine.hpp"

using namespace emu;
using emu::test::TestMachine;

namespace {

using Status = StepResult::Status;

constexpr std::uint16_t kDs = 0x3000;
constexpr std::uint16_t kSs = 0x4000;
constexpr std::uint16_t kEs = 0x5000;

// Segmenti dei dati distinti da CS, così ogni test vede da quale segmento si legge.
void set_data_segments(TestMachine& m) {
    m.regs().set(SegReg::DS, kDs);
    m.regs().set(SegReg::SS, kSs);
    m.regs().set(SegReg::ES, kEs);
}

}  // namespace

TEST_CASE("gli otto modi di indirizzamento leggono dall'indirizzo giusto", "[cpu][memory]") {
    struct Case {
        std::uint8_t rm;
        std::uint16_t segment;
        std::uint16_t offset;  // con BX=1000h, BP=2000h, SI=0100h, DI=0010h e spiazzamento +4
    };
    const Case cases[] = {
        {0, kDs, 0x1104}, {1, kDs, 0x1014}, {2, kSs, 0x2104}, {3, kSs, 0x2014},
        {4, kDs, 0x0104}, {5, kDs, 0x0014}, {6, kSs, 0x2004}, {7, kDs, 0x1004},
    };
    for (const Case& c : cases) {
        INFO("r/m " << int{c.rm});
        // MOV AX, [... + 4]: mod = 01, reg = AX
        TestMachine m({0x8B, static_cast<std::uint8_t>(0x40 | c.rm), 0x04});
        set_data_segments(m);
        m.regs().set(Reg16::BX, 0x1000);
        m.regs().set(Reg16::BP, 0x2000);
        m.regs().set(Reg16::SI, 0x0100);
        m.regs().set(Reg16::DI, 0x0010);
        m.memory.write16(c.segment, c.offset, static_cast<std::uint16_t>(0xA000 + c.rm));
        CHECK(m.step().status == Status::Executed);
        CHECK(m.regs().get(Reg16::AX) == 0xA000 + c.rm);
    }
}

TEST_CASE("indirizzo diretto a 16 bit", "[cpu][memory]") {
    TestMachine m({0x8B, 0x06, 0x34, 0x12});  // MOV AX, [1234h]
    set_data_segments(m);
    m.memory.write16(kDs, 0x1234, 0xBEEF);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0xBEEF);
    CHECK(m.regs().ip == 0x0104);
}

TEST_CASE("l'indirizzo effettivo fa wrap a 16 bit dentro il segmento", "[cpu][memory]") {
    TestMachine m({
        0x8A, 0x00,        // MOV AL, [BX+SI]   FFFFh + 0002h = 0001h
        0x8A, 0x67, 0xFE,  // MOV AH, [BX-2]    0001h - 2 = FFFFh
    });
    set_data_segments(m);
    m.regs().set(Reg16::BX, 0xFFFF);
    m.regs().set(Reg16::SI, 0x0002);
    m.memory.write8(kDs, 0x0001, 0x5A);
    m.memory.poke(physical_address(kDs, 0xFFFF) + 2, 0xEE);  // DS*16 + 10001h: non va letto
    m.step();
    CHECK(m.regs().get(Reg8::AL) == 0x5A);

    m.regs().set(Reg16::BX, 0x0001);
    m.memory.write8(kDs, 0xFFFF, 0xC3);
    m.step();
    CHECK(m.regs().get(Reg8::AH) == 0xC3);
}

TEST_CASE("una parola all'offset FFFF prende il byte alto dall'inizio del segmento", "[cpu][memory]") {
    TestMachine m({0x8B, 0x07});  // MOV AX, [BX]
    set_data_segments(m);
    m.regs().set(Reg16::BX, 0xFFFF);
    m.memory.write8(kDs, 0xFFFF, 0x34);
    m.memory.write8(kDs, 0x0000, 0x12);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x1234);
}

TEST_CASE("il prefisso sostituisce il segmento predefinito", "[cpu][memory][prefix]") {
    TestMachine m({
        0x26, 0x8B, 0x07,        // MOV AX, ES:[BX]
        0x3E, 0x8B, 0x4E, 0x00,  // MOV CX, DS:[BP+0]
        0x2E, 0x8B, 0x17,        // MOV DX, CS:[BX]
    });
    set_data_segments(m);
    m.regs().set(Reg16::BX, 0x0010);
    m.regs().set(Reg16::BP, 0x0020);
    m.memory.write16(kEs, 0x0010, 0x1111);
    m.memory.write16(kDs, 0x0020, 0x2222);
    m.memory.write16(TestMachine::kCodeSegment, 0x0010, 0x3333);

    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x1111);
    m.step();
    CHECK(m.regs().get(Reg16::CX) == 0x2222);
    m.step();
    CHECK(m.regs().get(Reg16::DX) == 0x3333);
    CHECK(m.regs().ip == 0x010A);  // i prefissi contano nella lunghezza
}

TEST_CASE("MOV verso la memoria, little-endian", "[cpu][memory]") {
    TestMachine m({
        0x89, 0x07,                    // MOV [BX], AX
        0x88, 0x67, 0x02,              // MOV [BX+2], AH
        0xC6, 0x47, 0x03, 0x42,        // MOV BYTE [BX+3], 42h
        0xC7, 0x47, 0x04, 0xCD, 0xAB,  // MOV WORD [BX+4], ABCDh
    });
    set_data_segments(m);
    m.regs().set(Reg16::BX, 0x0100);
    m.regs().set(Reg16::AX, 0x1234);
    for (int i = 0; i < 4; ++i) {
        CHECK(m.step().status == Status::Executed);
    }
    CHECK(m.memory.read8(kDs, 0x0100) == 0x34);
    CHECK(m.memory.read8(kDs, 0x0101) == 0x12);
    CHECK(m.memory.read8(kDs, 0x0102) == 0x12);
    CHECK(m.memory.read8(kDs, 0x0103) == 0x42);
    CHECK(m.memory.read16(kDs, 0x0104) == 0xABCD);
}

TEST_CASE("ALU con un operando in memoria", "[cpu][memory]") {
    TestMachine m({
        0x01, 0x07,        // ADD [BX], AX
        0x03, 0x0F,        // ADD CX, [BX]
        0x39, 0x0F,        // CMP [BX], CX
        0x80, 0x7F, 0x02, 0x05,  // CMP BYTE [BX+2], 5
        0x83, 0x2F, 0x01,  // SUB WORD [BX], 1
    });
    set_data_segments(m);
    m.regs().set(Reg16::BX, 0x0200);
    m.regs().set(Reg16::AX, 0x0001);
    m.memory.write16(kDs, 0x0200, 0x00FF);
    m.memory.write8(kDs, 0x0202, 0x03);

    m.step();
    CHECK(m.memory.read16(kDs, 0x0200) == 0x0100);
    m.step();
    CHECK(m.regs().get(Reg16::CX) == 0x0100);
    m.step();
    CHECK(m.memory.read16(kDs, 0x0200) == 0x0100);  // CMP non scrive
    CHECK(m.regs().flags.test(Flag::ZF));
    m.step();
    CHECK(m.memory.read8(kDs, 0x0202) == 0x03);
    CHECK(m.regs().flags.test(Flag::CF));  // 3 < 5
    m.step();
    CHECK(m.memory.read16(kDs, 0x0200) == 0x00FF);
}

TEST_CASE("82 si comporta come 80", "[cpu][memory]") {
    TestMachine m({
        0x82, 0xC0, 0x10,        // ADD AL, 10h
        0x82, 0x2F, 0x01,        // SUB BYTE [BX], 1
        0x82, 0x7F, 0x01, 0x05,  // CMP BYTE [BX+1], 5
    });
    set_data_segments(m);
    m.regs().set(Reg16::AX, 0x00F8);
    m.regs().set(Reg16::BX, 0x0400);
    m.memory.write8(kDs, 0x0400, 0x00);
    m.memory.write8(kDs, 0x0401, 0x05);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x0008);
    CHECK(m.regs().flags.test(Flag::CF));
    m.step();
    CHECK(m.memory.read8(kDs, 0x0400) == 0xFF);
    m.step();
    CHECK(m.regs().flags.test(Flag::ZF));
    CHECK(m.memory.read8(kDs, 0x0401) == 0x05);
}

TEST_CASE("INC, DEC, NEG, NOT su memoria", "[cpu][memory]") {
    TestMachine m({
        0xFE, 0x07,  // INC BYTE [BX]
        0xFF, 0x4F, 0x02,  // DEC WORD [BX+2]
        0xF6, 0x5F, 0x04,  // NEG BYTE [BX+4]
        0xF7, 0x57, 0x06,  // NOT WORD [BX+6]
    });
    set_data_segments(m);
    m.regs().set(Reg16::BX, 0x0300);
    m.memory.write8(kDs, 0x0300, 0xFF);
    m.memory.write16(kDs, 0x0302, 0x0000);
    m.memory.write8(kDs, 0x0304, 0x01);
    m.memory.write16(kDs, 0x0306, 0x00FF);
    for (int i = 0; i < 4; ++i) {
        CHECK(m.step().status == Status::Executed);
    }
    CHECK(m.memory.read8(kDs, 0x0300) == 0x00);
    CHECK(m.memory.read8(kDs, 0x0301) == 0x00);  // INC a 8 bit non tocca il byte dopo
    CHECK(m.memory.read16(kDs, 0x0302) == 0xFFFF);
    CHECK(m.memory.read8(kDs, 0x0304) == 0xFF);
    CHECK(m.memory.read16(kDs, 0x0306) == 0xFF00);
}

TEST_CASE("MOV con i registri di segmento", "[cpu][memory]") {
    TestMachine m({
        0x8E, 0xD8,        // MOV DS, AX
        0xA1, 0x00, 0x01,  // MOV AX, [0100h]  (letto con il nuovo DS)
        0x8C, 0xC9,        // MOV CX, CS
        0x8C, 0x1F,        // MOV [BX], DS
    });
    m.regs().set(Reg16::AX, 0x6000);
    m.regs().set(Reg16::BX, 0x0010);
    m.memory.write16(0x6000, 0x0100, 0x4321);
    m.step();
    CHECK(m.regs().get(SegReg::DS) == 0x6000);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x4321);
    m.step();
    CHECK(m.regs().get(Reg16::CX) == TestMachine::kCodeSegment);
    m.step();
    CHECK(m.memory.read16(0x6000, 0x0010) == 0x6000);
}

TEST_CASE("A0-A3 rispettano il prefisso", "[cpu][memory][prefix]") {
    TestMachine m({
        0x26, 0xA0, 0x00, 0x01,  // MOV AL, ES:[0100h]
        0xA3, 0x02, 0x01,        // MOV [0102h], AX
    });
    set_data_segments(m);
    m.regs().set(Reg16::AX, 0x1200);
    m.memory.write8(kEs, 0x0100, 0x34);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x1234);
    m.step();
    CHECK(m.memory.read16(kDs, 0x0102) == 0x1234);
}

TEST_CASE("un prefisso su un'istruzione senza memoria non ha effetto", "[cpu][prefix]") {
    TestMachine m({0x26, 0xB8, 0x34, 0x12});  // ES: MOV AX, 1234h
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0x1234);
    CHECK(m.regs().ip == 0x0104);
}

TEST_CASE("opcode non implementato dopo un prefisso: stato invariato", "[cpu][prefix]") {
    TestMachine m({0x26, 0x10, 0xC0});  // ES: ADC AL, AL
    const Registers before = m.regs();
    const StepResult r = m.step();
    CHECK(r.status == Status::Unimplemented);
    CHECK(r.opcode == 0x10);
    CHECK(r.ip == 0x0100);  // inizio dell'istruzione, cioè il prefisso
    CHECK(m.regs() == before);
}

TEST_CASE("MOV CS, r/m non è implementata", "[cpu][memory]") {
    TestMachine m({0x8E, 0xC8});  // MOV CS, AX
    m.regs().set(Reg16::AX, 0x2000);
    const Registers before = m.regs();
    CHECK(m.step().status == Status::Unimplemented);
    CHECK(m.regs() == before);
}
