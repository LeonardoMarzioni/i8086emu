#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/cpu.hpp"
#include "test_machine.hpp"

using namespace emu;
using emu::test::TestMachine;

namespace {

using Status = StepResult::Status;

constexpr std::uint16_t kSs = 0x2000;
constexpr std::uint16_t kDs = 0x3000;

// Stack in un segmento proprio, con SP come all'avvio di un programma .COM.
void set_stack(TestMachine& m, std::uint16_t sp = 0xFFFE) {
    m.regs().set(SegReg::SS, kSs);
    m.regs().set(SegReg::DS, kDs);
    m.regs().set(Reg16::SP, sp);
}

// Esegue fino a HLT, con un limite per non restare in un ciclo infinito.
StepResult run_until_halt(TestMachine& m, int max_steps = 1000) {
    StepResult r;
    for (int i = 0; i < max_steps; ++i) {
        r = m.step();
        if (r.status != Status::Executed) {
            break;
        }
    }
    return r;
}

}  // namespace

TEST_CASE("PUSH e POP passano per SS:SP", "[cpu][stack]") {
    TestMachine m({
        0x50,  // PUSH AX
        0x5B,  // POP BX
    });
    set_stack(m);
    m.regs().set(Reg16::AX, 0x1234);
    m.step();
    CHECK(m.regs().get(Reg16::SP) == 0xFFFC);
    CHECK(m.memory.read16(kSs, 0xFFFC) == 0x1234);
    m.step();
    CHECK(m.regs().get(Reg16::BX) == 0x1234);
    CHECK(m.regs().get(Reg16::SP) == 0xFFFE);
}

TEST_CASE("PUSH SP salva il valore già decrementato (comportamento 8086)", "[cpu][stack]") {
    TestMachine m({0x54});  // PUSH SP
    set_stack(m, 0x0100);
    m.step();
    CHECK(m.regs().get(Reg16::SP) == 0x00FE);
    CHECK(m.memory.read16(kSs, 0x00FE) == 0x00FE);  // su 80286 e successivi sarebbe 0100h
}

TEST_CASE("POP SP: vince il valore letto dallo stack", "[cpu][stack]") {
    TestMachine m({0x5C});  // POP SP
    set_stack(m, 0x0100);
    m.memory.write16(kSs, 0x0100, 0x5555);
    m.step();
    CHECK(m.regs().get(Reg16::SP) == 0x5555);
}

TEST_CASE("lo stack fa wrap a 16 bit dentro SS", "[cpu][stack]") {
    TestMachine m({
        0x50,  // PUSH AX con SP = 0000h
        0x58,  // POP AX
    });
    set_stack(m, 0x0000);
    m.regs().set(Reg16::AX, 0xABCD);
    m.step();
    CHECK(m.regs().get(Reg16::SP) == 0xFFFE);
    CHECK(m.memory.read16(kSs, 0xFFFE) == 0xABCD);
    m.regs().set(Reg16::AX, 0);
    m.step();
    CHECK(m.regs().get(Reg16::AX) == 0xABCD);
    CHECK(m.regs().get(Reg16::SP) == 0x0000);
}

TEST_CASE("PUSH con SP dispari: la parola è a cavallo della fine di SS", "[cpu][stack]") {
    TestMachine m({0x50});  // PUSH AX con SP = 0001h
    set_stack(m, 0x0001);
    m.regs().set(Reg16::AX, 0x1234);
    m.step();
    CHECK(m.regs().get(Reg16::SP) == 0xFFFF);
    CHECK(m.memory.read8(kSs, 0xFFFF) == 0x34);
    CHECK(m.memory.read8(kSs, 0x0000) == 0x12);
}

TEST_CASE("un prefisso non cambia il segmento dello stack", "[cpu][stack][prefix]") {
    TestMachine m({0x26, 0x50});  // ES: PUSH AX
    set_stack(m);
    m.regs().set(SegReg::ES, 0x5000);
    m.regs().set(Reg16::AX, 0x4321);
    m.step();
    CHECK(m.memory.read16(kSs, 0xFFFC) == 0x4321);
    CHECK(m.memory.read16(0x5000, 0xFFFC) == 0x0000);
}

TEST_CASE("JMP short e near, in avanti e all'indietro", "[cpu][jump]") {
    TestMachine m({
        0xEB, 0x03,        // 0100: JMP 0105h
        0xF4,              // 0102: HLT (saltata)
        0x90, 0x90,        // 0103
        0xE9, 0x02, 0x00,  // 0105: JMP 010Ah
        0xF4, 0xF4,        // 0108
        0xEB, 0xF6,        // 010A: JMP 0102h
    });
    m.step();
    CHECK(m.regs().ip == 0x0105);
    m.step();
    CHECK(m.regs().ip == 0x010A);
    m.step();
    CHECK(m.regs().ip == 0x0102);
    CHECK(m.step().status == Status::Halted);
}

TEST_CASE("la destinazione di un salto fa wrap a 16 bit", "[cpu][jump]") {
    TestMachine m(std::vector<std::uint8_t>{});
    m.memory.write8(TestMachine::kCodeSegment, 0xFFF0, 0xEB);  // JMP short +7Fh
    m.memory.write8(TestMachine::kCodeSegment, 0xFFF1, 0x7F);
    m.regs().ip = 0xFFF0;
    m.step();
    CHECK(m.regs().ip == 0x0071);
}

TEST_CASE("JZ e JNZ dipendono solo da ZF e non modificano i flag", "[cpu][jump]") {
    for (const bool zero : {false, true}) {
        INFO("ZF = " << zero);
        TestMachine jz({0x74, 0x10});
        jz.regs().flags.set(Flag::ZF, zero);
        jz.regs().flags.set(Flag::CF, true);
        const std::uint16_t flags_before = jz.regs().flags.word();
        jz.step();
        CHECK(jz.regs().ip == (zero ? 0x0112 : 0x0102));
        CHECK(jz.regs().flags.word() == flags_before);

        TestMachine jnz({0x75, 0x10});
        jnz.regs().flags.set(Flag::ZF, zero);
        jnz.step();
        CHECK(jnz.regs().ip == (zero ? 0x0102 : 0x0112));
    }
}

TEST_CASE("un ciclo con DEC e JNZ", "[cpu][jump]") {
    TestMachine m({
        0xB9, 0x05, 0x00,  // 0100: MOV CX, 5
        0x31, 0xC0,        // 0103: XOR AX, AX
        0x01, 0xC8,        // 0105: ADD AX, CX
        0x49,              // 0107: DEC CX
        0x75, 0xFB,        // 0108: JNZ 0105h
        0xF4,              // 010A: HLT
    });
    const StepResult r = run_until_halt(m);
    CHECK(r.status == Status::Halted);
    CHECK(r.ip == 0x010A);
    CHECK(m.regs().get(Reg16::AX) == 15);  // 5 + 4 + 3 + 2 + 1
    CHECK(m.regs().get(Reg16::CX) == 0);
}

TEST_CASE("CALL salva l'indirizzo di ritorno e RET lo riprende", "[cpu][call]") {
    TestMachine m({
        0xB8, 0x03, 0x00,  // 0100: MOV AX, 3
        0xE8, 0x04, 0x00,  // 0103: CALL 010Ah
        0xE8, 0x01, 0x00,  // 0106: CALL 010Ah
        0xF4,              // 0109: HLT
        0x01, 0xC0,        // 010A: ADD AX, AX
        0xC3,              // 010C: RET
    });
    set_stack(m);
    m.step();
    m.step();  // prima CALL
    CHECK(m.regs().ip == 0x010A);
    CHECK(m.regs().get(Reg16::SP) == 0xFFFC);
    CHECK(m.memory.read16(kSs, 0xFFFC) == 0x0106);

    const StepResult r = run_until_halt(m);
    CHECK(r.status == Status::Halted);
    CHECK(r.ip == 0x0109);
    CHECK(m.regs().get(Reg16::AX) == 12);
    CHECK(m.regs().get(Reg16::SP) == 0xFFFE);
}

TEST_CASE("CALL annidate", "[cpu][call]") {
    TestMachine m({
        0xE8, 0x01, 0x00,  // 0100: CALL 0104h
        0xF4,              // 0103: HLT
        0xE8, 0x01, 0x00,  // 0104: CALL 0108h
        0xC3,              // 0107: RET
        0x40,              // 0108: INC AX
        0xC3,              // 0109: RET
    });
    set_stack(m);
    const StepResult r = run_until_halt(m);
    CHECK(r.status == Status::Halted);
    CHECK(r.ip == 0x0103);
    CHECK(m.regs().get(Reg16::AX) == 1);
    CHECK(m.regs().get(Reg16::SP) == 0xFFFE);
}

TEST_CASE("CALL e JMP indirette, da registro e da memoria", "[cpu][call]") {
    TestMachine m({
        0xFF, 0xD3,        // 0100: CALL BX       -> 0110h
        0xF4,              // 0102: HLT
    });
    set_stack(m);
    m.regs().set(Reg16::BX, 0x0110);
    m.memory.load(physical_address(TestMachine::kCodeSegment, 0x0110),
                  std::vector<std::uint8_t>{
                      0xFF, 0x67, 0x02,  // 0110: JMP [BX+2]   -> parola in DS:0012h
                  });
    m.memory.write16(kDs, 0x0012, 0x0120);
    m.memory.write8(TestMachine::kCodeSegment, 0x0120, 0xC3);  // 0120: RET

    m.step();
    CHECK(m.regs().ip == 0x0110);
    CHECK(m.memory.read16(kSs, 0xFFFC) == 0x0102);
    m.regs().set(Reg16::BX, 0x0010);
    m.step();
    CHECK(m.regs().ip == 0x0120);
    m.step();
    CHECK(m.regs().ip == 0x0102);
}

TEST_CASE("CALL SP usa il valore di SP prima del push", "[cpu][call]") {
    TestMachine m({0xFF, 0xD4});  // CALL SP
    set_stack(m, 0x0200);
    m.step();
    CHECK(m.regs().ip == 0x0200);
    CHECK(m.regs().get(Reg16::SP) == 0x01FE);
    CHECK(m.memory.read16(kSs, 0x01FE) == 0x0102);
}

TEST_CASE("CALL e JMP far non sono implementate", "[cpu][call]") {
    TestMachine m({0xFF, 0x1F});  // CALL FAR [BX]
    set_stack(m);
    const Registers before = m.regs();
    const StepResult r = m.step();
    CHECK(r.status == Status::Unimplemented);
    CHECK(r.group_reg == 3);
    CHECK(m.regs() == before);
}
