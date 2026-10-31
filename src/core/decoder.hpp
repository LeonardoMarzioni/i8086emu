#pragma once

#include <cstdint>
#include <optional>

#include "core/memory.hpp"
#include "core/opcode_table.hpp"
#include "core/registers.hpp"
#include "core/types.hpp"

namespace emu {

// Numero massimo di prefissi accettati prima dell'opcode. L'8086 non ha un limite, ma un
// segmento pieno di prefissi farebbe girare la decodifica all'infinito: oltre questo numero
// l'istruzione viene segnalata come non implementata.
inline constexpr std::uint8_t kMaxPrefixes = 8;

// Operando in memoria, in forma simbolica. L'indirizzo effettivo si calcola al momento
// dell'esecuzione, perché dipende dai registri; il disassemblatore lo mostra così com'è.
struct MemoryRef {
    std::optional<Reg16> base;       // BX o BP
    std::optional<Reg16> index;      // SI o DI
    std::uint16_t displacement = 0;  // già esteso con segno se a 8 bit
    std::uint8_t displacement_size = 0;  // 0, 1 o 2 byte nell'istruzione
    SegReg segment = SegReg::DS;     // segmento effettivo: prefisso, oppure SS se la base è BP, oppure DS

    bool operator==(const MemoryRef&) const = default;
};

// Operando già risolto: il registro concreto, il valore dell'immediato, il riferimento
// alla memoria o la destinazione di un salto relativo.
struct Operand {
    enum class Type : std::uint8_t { None, Reg8, Reg16, Seg, Imm, Mem, Target };

    Type type = Type::None;
    std::uint8_t reg = 0;    // Reg8/Reg16/Seg: indice nella codifica ModR/M
    std::uint16_t imm = 0;   // Imm: valore, già esteso con segno per Ibs
                             // Target: offset assoluto della destinazione in CS
    MemoryRef mem;           // Mem
};

struct Instruction {
    std::uint16_t cs = 0;     // indirizzo del primo byte, prefissi compresi
    std::uint16_t ip = 0;
    std::uint8_t length = 0;  // byte totali, prefissi compresi
    std::uint8_t opcode = 0;  // il byte di opcode, dopo gli eventuali prefissi
    std::optional<SegReg> segment_override;  // dall'ultimo prefisso, se presente
    std::optional<std::uint8_t> group_reg;   // campo reg del ModR/M, solo per gli opcode di gruppo
    Mnemonic mnemonic = Mnemonic::Unimplemented;
    Width width = Width::Byte;
    Operand dst;
    Operand src;
    // Tipi di operando della tabella: servono al disassemblatore (ad esempio per scrivere
    // "byte -0x1" con l'opcode 83 o "jmp short" con EB).
    OperandKind dst_kind = OperandKind::None;
    OperandKind src_kind = OperandKind::None;
};

// Decodifica l'istruzione che inizia a cs:ip.
// Funzione pura: legge la memoria ma non modifica nulla. Per questo il disassemblatore
// la può riusare così com'è, e un opcode non implementato lascia la CPU com'era.
Instruction decode(const Memory& memory, std::uint16_t cs, std::uint16_t ip);

}  // namespace emu
