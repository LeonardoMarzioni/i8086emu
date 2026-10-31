#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace emu {

enum class Mnemonic : std::uint8_t {
    Unimplemented,  // opcode fuori dal sottoinsieme, o forma non ancora supportata
    SegmentPrefix,  // 26, 2E, 36, 3E: prefisso di segment override
    // Opcode di gruppo: il campo reg del ModR/M sceglie l'istruzione nella sotto-tabella.
    // I numeri seguono le mappe opcode Intel.
    Group1,   // 80, 81, 83: operazione ALU con immediato
    Group82,  // 82: come 80, ma solo con le operazioni documentate dal manuale
    Group3,   // F6/F7: NOT, NEG (TEST, MUL, DIV... fuori dal sottoinsieme)
    Group4,   // FE: INC/DEC a 8 bit
    Group5,   // FF: INC/DEC a 16 bit, CALL e JMP near indirette
    Group11,  // C6/C7: MOV con immediato, valido solo con reg = 0
    Mov,
    Add,
    Or,
    And,
    Sub,
    Xor,
    Cmp,
    Inc,
    Dec,
    Neg,
    Not,
    Nop,
    Hlt,
    Push,
    Pop,
    Jmp,
    Jz,   // anche JE: stesso opcode 74
    Jnz,  // anche JNE: stesso opcode 75
    Call,
    Ret,
    Count  // sentinella: numero di mnemonici
};

inline constexpr std::size_t kMnemonicCount = static_cast<std::size_t>(Mnemonic::Count);

// Tipi di operando, nella notazione delle mappe opcode Intel. La tabella si può così
// confrontare riga per riga con il manuale.
enum class OperandKind : std::uint8_t {
    None,
    Eb, Ev,   // registro o memoria dal campo r/m del ModR/M, 8/16 bit
    Ew,       // come Ev, ma sempre a 16 bit (8C/8E)
    Gb, Gv,   // registro dal campo reg del ModR/M, 8/16 bit
    Sw,       // registro di segmento dal campo reg del ModR/M (8C/8E)
    Ib, Iv,   // immediato a 8/16 bit
    Ibs,      // immediato a 8 bit esteso con segno a 16 (opcode 83)
    Zb, Zv,   // registro nei 3 bit bassi dell'opcode (B0+r, B8+r, 40+r, 48+r)
    AL, AX,   // accumulatore implicito
    Ob, Ov,   // memoria a indirizzo diretto [disp16], senza ModR/M (A0-A3)
    Jb, Jv,   // spiazzamento relativo a 8 bit (con segno) o 16 bit di un salto near
};

struct OpcodeInfo {
    Mnemonic mnemonic = Mnemonic::Unimplemented;
    OperandKind dst = OperandKind::None;
    OperandKind src = OperandKind::None;
};

using GroupTable = std::array<Mnemonic, 8>;

// Sotto-tabelle dei gruppi, indicizzate dal campo reg del ModR/M.
inline constexpr GroupTable kGroup1 = {
    Mnemonic::Add, Mnemonic::Or,
    Mnemonic::Unimplemented,  // ADC
    Mnemonic::Unimplemented,  // SBB
    Mnemonic::And, Mnemonic::Sub, Mnemonic::Xor, Mnemonic::Cmp,
};

// 82: la Tabella 4-13 del manuale documenta ADD, ADC, SBB, SUB, CMP e indica /1, /4, /6
// come "not used". Disallineamento con l'hardware: l'8086 reale esegue anche /1, /4, /6
// come 80 (OR, AND, XOR); qui seguiamo la documentazione.
inline constexpr GroupTable kGroup82 = {
    Mnemonic::Add,
    Mnemonic::Unimplemented,  // /1: not used (sull'hardware OR)
    Mnemonic::Unimplemented,  // ADC
    Mnemonic::Unimplemented,  // SBB
    Mnemonic::Unimplemented,  // /4: not used (sull'hardware AND)
    Mnemonic::Sub,
    Mnemonic::Unimplemented,  // /6: not used (sull'hardware XOR)
    Mnemonic::Cmp,
};

inline constexpr GroupTable kGroup3 = {
    Mnemonic::Unimplemented,  // TEST
    Mnemonic::Unimplemented,  // alias di TEST, non documentato
    Mnemonic::Not, Mnemonic::Neg,
    Mnemonic::Unimplemented,  // MUL
    Mnemonic::Unimplemented,  // IMUL
    Mnemonic::Unimplemented,  // DIV
    Mnemonic::Unimplemented,  // IDIV
};

inline constexpr GroupTable kGroup4 = {
    Mnemonic::Inc, Mnemonic::Dec,
    // 2..7: forme non definite sull'8086
    Mnemonic::Unimplemented, Mnemonic::Unimplemented, Mnemonic::Unimplemented,
    Mnemonic::Unimplemented, Mnemonic::Unimplemented, Mnemonic::Unimplemented,
};

inline constexpr GroupTable kGroup5 = {
    Mnemonic::Inc, Mnemonic::Dec,
    Mnemonic::Call,           // CALL near indiretta: CALL r/m16
    Mnemonic::Unimplemented,  // CALL far indiretta
    Mnemonic::Jmp,            // JMP near indiretta: JMP r/m16
    Mnemonic::Unimplemented,  // JMP far indiretta
    Mnemonic::Unimplemented,  // PUSH r/m16
    Mnemonic::Unimplemented,  // alias di PUSH, non documentato
};

inline constexpr GroupTable kGroup11 = {
    Mnemonic::Mov,
    // 1..7: forme non definite sull'8086
    Mnemonic::Unimplemented, Mnemonic::Unimplemented, Mnemonic::Unimplemented,
    Mnemonic::Unimplemented, Mnemonic::Unimplemented, Mnemonic::Unimplemented,
    Mnemonic::Unimplemented,
};

// Tabella principale, indicizzata dal byte di opcode.
extern const std::array<OpcodeInfo, 256> kOpcodeTable;

}  // namespace emu
