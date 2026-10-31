#include "core/opcode_table.hpp"

namespace emu {
namespace {

// La tabella è costruita a tempo di compilazione. I cicli rendono visibile la regolarità
// della codifica dell'8086 invece di elencare 256 voci a mano.
constexpr std::array<OpcodeInfo, 256> build_opcode_table() {
    using enum Mnemonic;
    using enum OperandKind;
    std::array<OpcodeInfo, 256> t{};  // tutte le voci partono come Unimplemented

    // 00..3F: otto operazioni ALU in sei forme. I bit 5..3 scelgono l'operazione (ordine del
    // gruppo 1), i bit 2..0 la forma:
    //   0: Eb,Gb   1: Ev,Gv   2: Gb,Eb   3: Gv,Ev   4: AL,Ib   5: AX,Iv
    // Le forme 6 e 7 sono altre istruzioni; qui sotto ci sono solo i prefissi.
    for (std::size_t op = 0; op < 8; ++op) {
        const Mnemonic m = kGroup1[op];
        if (m == Unimplemented) {
            continue;  // ADC, SBB
        }
        const std::size_t base = op << 3;
        t[base + 0] = {m, Eb, Gb};
        t[base + 1] = {m, Ev, Gv};
        t[base + 2] = {m, Gb, Eb};
        t[base + 3] = {m, Gv, Ev};
        t[base + 4] = {m, AL, Ib};
        t[base + 5] = {m, AX, Iv};
    }

    // Prefissi di segment override, codificati 001 sr 110: i bit 4..3 sono il registro
    // di segmento nella stessa codifica del ModR/M (ES, CS, SS, DS).
    t[0x26] = {SegmentPrefix, None, None};
    t[0x2E] = {SegmentPrefix, None, None};
    t[0x36] = {SegmentPrefix, None, None};
    t[0x3E] = {SegmentPrefix, None, None};

    // 40..5F: INC, DEC, PUSH, POP di un registro a 16 bit, codificato nei 3 bit bassi.
    for (std::size_t r = 0; r < 8; ++r) {
        t[0x40 + r] = {Inc, Zv, None};
        t[0x48 + r] = {Dec, Zv, None};
        t[0x50 + r] = {Push, Zv, None};
        t[0x58 + r] = {Pop, Zv, None};
    }

    // Salti condizionati: del gruppo 70..7F il sottoinsieme ha solo JZ/JE e JNZ/JNE.
    t[0x74] = {Jz, Jb, None};
    t[0x75] = {Jnz, Jb, None};

    // 80..83: gruppo 1.
    // 82 è come 80 solo per ADD, ADC, SBB, SUB, CMP (Tabella 4-13): sotto-tabella kGroup82.
    // 83 estende l'immediato con segno; con reg = 1, 4, 6 la Tabella 4-13 lo dà "not used",
    // ma il sottoinsieme lo richiede e la suite lo classifica "normal".
    t[0x80] = {Group1, Eb, Ib};
    t[0x81] = {Group1, Ev, Iv};
    t[0x82] = {Group82, Eb, Ib};
    t[0x83] = {Group1, Ev, Ibs};

    // 88..8B: MOV tra registro e r/m, nelle stesse quattro forme d/w delle ALU.
    t[0x88] = {Mov, Eb, Gb};
    t[0x89] = {Mov, Ev, Gv};
    t[0x8A] = {Mov, Gb, Eb};
    t[0x8B] = {Mov, Gv, Ev};

    // 8C/8E: MOV con un registro di segmento, ModR/M nel formato "mod 0 sr r/m"
    // (Tabella 4-13). Il decoder rifiuta reg 4..7 ("not used") e CS come destinazione.
    t[0x8C] = {Mov, Ew, Sw};
    t[0x8E] = {Mov, Sw, Ew};

    // 90: NOP. In realtà è XCHG AX,AX, che non ha effetti.
    t[0x90] = {Nop, None, None};

    // A0..A3: MOV tra accumulatore e memoria a indirizzo diretto, senza ModR/M.
    t[0xA0] = {Mov, AL, Ob};
    t[0xA1] = {Mov, AX, Ov};
    t[0xA2] = {Mov, Ob, AL};
    t[0xA3] = {Mov, Ov, AX};

    // B0..BF: MOV registro, immediato. Il bit 3 è "w", i bit 2..0 il registro.
    for (std::size_t r = 0; r < 8; ++r) {
        t[0xB0 + r] = {Mov, Zb, Ib};
        t[0xB8 + r] = {Mov, Zv, Iv};
    }

    // C6/C7: MOV r/m, immediato (gruppo 11).
    t[0xC6] = {Group11, Eb, Ib};
    t[0xC7] = {Group11, Ev, Iv};

    // Trasferimenti near: RET senza operandi, CALL e JMP con spiazzamento relativo.
    // Le forme far (CA, CB, 9A, EA) e RET con immediato (C2) sono fuori dal sottoinsieme.
    t[0xC3] = {Ret, None, None};
    t[0xE8] = {Call, Jv, None};
    t[0xE9] = {Jmp, Jv, None};
    t[0xEB] = {Jmp, Jb, None};

    t[0xF4] = {Hlt, None, None};

    // Gruppi a un operando.
    t[0xF6] = {Group3, Eb, None};
    t[0xF7] = {Group3, Ev, None};
    t[0xFE] = {Group4, Eb, None};
    t[0xFF] = {Group5, Ev, None};

    return t;
}

}  // namespace

constinit const std::array<OpcodeInfo, 256> kOpcodeTable = build_opcode_table();

}  // namespace emu
