#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/disassembler.hpp"

using namespace emu;

// Il testo atteso è quello di ndisasm 2.16.01 sugli stessi byte (verificato a mano), tranne
// per 82, che ndisasm non decodifica. Il confronto automatico con ndisasm sui programmi di
// prova è in tests/programs/compare_disasm.sh.

namespace {

DisassembledLine disassemble_bytes(const std::vector<std::uint8_t>& bytes) {
    Memory mem;
    mem.load(physical_address(0x1000, 0x0100), bytes);
    return disassemble(mem, 0x1000, 0x0100);
}

struct Case {
    std::vector<std::uint8_t> bytes;
    std::string text;
};

void check_cases(const std::vector<Case>& cases) {
    for (const Case& c : cases) {
        INFO(c.text);
        const DisassembledLine line = disassemble_bytes(c.bytes);
        CHECK(line.text == c.text);
        CHECK(line.bytes == c.bytes);
    }
}

}  // namespace

TEST_CASE("disassembler: registri e immediati", "[disassembler]") {
    check_cases({
        {{0xB8, 0x34, 0x12}, "mov ax,0x1234"},
        {{0xB0, 0x05}, "mov al,0x5"},
        {{0x88, 0xC4}, "mov ah,al"},
        {{0x8A, 0xE0}, "mov ah,al"},
        {{0x81, 0xC3, 0x00, 0x01}, "add bx,0x100"},
        {{0x04, 0xFF}, "add al,0xff"},
        {{0x31, 0xC0}, "xor ax,ax"},
        {{0x83, 0xC0, 0xFF}, "add ax,byte -0x1"},
        {{0x83, 0xE3, 0x0F}, "and bx,byte +0xf"},
        {{0x82, 0xE8, 0x01}, "sub al,0x1"},
        {{0x40}, "inc ax"},
        {{0xFE, 0xC4}, "inc ah"},
        {{0xF6, 0xD8}, "neg al"},
        {{0xF7, 0xD3}, "not bx"},
        {{0x8C, 0xD8}, "mov ax,ds"},
        {{0x8E, 0xD0}, "mov ss,ax"},
    });
}

TEST_CASE("disassembler: operandi in memoria", "[disassembler]") {
    check_cases({
        {{0x8B, 0x00}, "mov ax,[bx+si]"},
        {{0x8B, 0x46, 0xFE}, "mov ax,[bp-0x2]"},
        {{0x8B, 0x46, 0x00}, "mov ax,[bp+0x0]"},
        {{0x8B, 0x87, 0x34, 0x12}, "mov ax,[bx+0x1234]"},
        {{0x8B, 0x87, 0xFE, 0xFF}, "mov ax,[bx-0x2]"},     // disp16 con segno, come ndisasm
        {{0x8B, 0x86, 0x00, 0x80}, "mov ax,[bp-0x8000]"},
        {{0x8B, 0x06, 0x34, 0x12}, "mov ax,[0x1234]"},
        {{0x89, 0x07}, "mov [bx],ax"},
        {{0xA1, 0x34, 0x12}, "mov ax,[0x1234]"},
        {{0xA2, 0x00, 0x01}, "mov [0x100],al"},
        {{0x8E, 0x1E, 0x34, 0x12}, "mov ds,[0x1234]"},
    });
}

TEST_CASE("disassembler: la dimensione si scrive solo se non è evidente", "[disassembler]") {
    check_cases({
        {{0xC6, 0x07, 0x42}, "mov byte [bx],0x42"},
        {{0xC7, 0x47, 0x02, 0x34, 0x12}, "mov word [bx+0x2],0x1234"},
        {{0x80, 0x3F, 0x05}, "cmp byte [bx],0x5"},
        {{0x83, 0x7E, 0xFC, 0x05}, "cmp word [bp-0x4],byte +0x5"},
        {{0xFE, 0x07}, "inc byte [bx]"},
        {{0xF7, 0x1F}, "neg word [bx]"},
        {{0x01, 0x07}, "add [bx],ax"},
    });
}

TEST_CASE("disassembler: prefissi di segmento", "[disassembler]") {
    check_cases({
        {{0x26, 0x8B, 0x07}, "mov ax,[es:bx]"},
        {{0x2E, 0xA1, 0x00, 0x01}, "mov ax,[cs:0x100]"},
        {{0x3E, 0x8B, 0x46, 0x00}, "mov ax,[ds:bp+0x0]"},
        {{0x26, 0xB8, 0x34, 0x12}, "es mov ax,0x1234"},
    });
}

TEST_CASE("disassembler: salti, chiamate e stack", "[disassembler]") {
    // Le istruzioni stanno a 1000:0100: le destinazioni sono offset assoluti.
    check_cases({
        {{0xEB, 0xFE}, "jmp short 0x100"},
        {{0xE9, 0xFD, 0xFF}, "jmp 0x100"},
        {{0x74, 0x05}, "jz 0x107"},
        {{0x75, 0xF0}, "jnz 0xf2"},
        {{0xE8, 0x00, 0x00}, "call 0x103"},
        {{0xFF, 0xD3}, "call bx"},
        {{0xFF, 0x17}, "call [bx]"},
        {{0xFF, 0x67, 0x02}, "jmp [bx+0x2]"},
        {{0xC3}, "ret"},
        {{0x50}, "push ax"},
        {{0x5F}, "pop di"},
        {{0x90}, "nop"},
        {{0xF4}, "hlt"},
    });
}

TEST_CASE("disassembler: un opcode non implementato occupa un solo byte", "[disassembler]") {
    const DisassembledLine adc = disassemble_bytes({0x10, 0xC0});  // ADC AL, AL
    CHECK(adc.text == "db 0x10");
    CHECK(adc.bytes == std::vector<std::uint8_t>{0x10});

    const DisassembledLine prefixed = disassemble_bytes({0x26, 0x10, 0xC0});
    CHECK(prefixed.text == "db 0x26");
    CHECK(prefixed.bytes.size() == 1);
}
