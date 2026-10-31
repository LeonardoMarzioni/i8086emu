#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/decoder.hpp"
#include "core/memory.hpp"

namespace emu {

// Testo di un'istruzione in sintassi NASM, nello stesso formato di ndisasm (l'oracolo dei
// test): minuscole, esadecimale con prefisso 0x e senza zeri iniziali, operandi separati da
// una virgola senza spazi. Esempi: "mov ax,0x1234", "cmp word [bp-0x4],byte +0x5",
// "jmp short 0x105", "mov ax,[es:bx]".
std::string format_instruction(const Instruction& ins);

struct DisassembledLine {
    Instruction instruction;
    std::vector<std::uint8_t> bytes;  // byte dell'istruzione, prefissi compresi
    std::string text;
};

// Disassembla l'istruzione a cs:ip riusando il decoder. Un opcode non implementato diventa
// "db 0x.." di un solo byte, come in ndisasm, così un elenco riprende dal byte successivo:
// per avanzare si usa bytes.size(), non instruction.length.
DisassembledLine disassemble(const Memory& memory, std::uint16_t cs, std::uint16_t ip);

}  // namespace emu
