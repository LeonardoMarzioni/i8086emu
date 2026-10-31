#include "core/decoder.hpp"

#include <array>

namespace emu {
namespace {

// Legge i byte dell'istruzione in sequenza. L'offset fa wrap a 16 bit dentro CS,
// come il registro IP dell'8086.
class ByteReader {
public:
    ByteReader(const Memory& memory, std::uint16_t cs, std::uint16_t ip) noexcept
        : memory_(memory), cs_(cs), ip_(ip) {}

    std::uint8_t next8() noexcept {
        ++count_;
        return memory_.read8(cs_, ip_++);
    }

    std::uint16_t next16() noexcept {
        const std::uint8_t low = next8();
        const std::uint8_t high = next8();
        return static_cast<std::uint16_t>(low | (high << 8));
    }

    std::uint8_t count() const noexcept { return count_; }

private:
    const Memory& memory_;
    std::uint16_t cs_;
    std::uint16_t ip_;
    std::uint8_t count_ = 0;
};

// Byte ModR/M: mod (2 bit) | reg (3 bit) | r/m (3 bit).
struct ModRM {
    std::uint8_t mod = 0;
    std::uint8_t reg = 0;
    std::uint8_t rm = 0;
};

ModRM split_modrm(std::uint8_t byte) noexcept {
    return {static_cast<std::uint8_t>(byte >> 6), static_cast<std::uint8_t>((byte >> 3) & 7),
            static_cast<std::uint8_t>(byte & 7)};
}

// Tabella 4-10 del manuale: con mod != 11 il campo r/m sceglie i registri da sommare.
// BX e BP sono registri base, SI e DI registri indice.
struct RmAddressing {
    std::optional<Reg16> base;
    std::optional<Reg16> index;
};

const std::array<RmAddressing, 8> kRmAddressing = {{
    {Reg16::BX, Reg16::SI},    // 000: [BX+SI]
    {Reg16::BX, Reg16::DI},    // 001: [BX+DI]
    {Reg16::BP, Reg16::SI},    // 010: [BP+SI]
    {Reg16::BP, Reg16::DI},    // 011: [BP+DI]
    {std::nullopt, Reg16::SI}, // 100: [SI]
    {std::nullopt, Reg16::DI}, // 101: [DI]
    {Reg16::BP, std::nullopt}, // 110: [BP], ma con mod = 00 è un indirizzo diretto
    {Reg16::BX, std::nullopt}, // 111: [BX]
}};

std::uint16_t sign_extend8(std::uint8_t value) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::int16_t>(static_cast<std::int8_t>(value)));
}

// Segmento di un dato in memoria (Tabella 2-2): quello del prefisso se c'è, altrimenti
// SS quando BP fa da base e DS in tutti gli altri casi.
SegReg data_segment(const MemoryRef& m, std::optional<SegReg> override) noexcept {
    return override.value_or(m.base == Reg16::BP ? SegReg::SS : SegReg::DS);
}

// Legge lo spiazzamento che segue il ModR/M e costruisce il riferimento alla memoria.
MemoryRef decode_memory(const ModRM& modrm, ByteReader& in, std::optional<SegReg> override) noexcept {
    MemoryRef m;
    if (modrm.mod == 0b00 && modrm.rm == 0b110) {
        // Eccezione della Tabella 4-8: indirizzo diretto a 16 bit, non [BP].
        m.displacement = in.next16();
        m.displacement_size = 2;
    } else {
        m.base = kRmAddressing[modrm.rm].base;
        m.index = kRmAddressing[modrm.rm].index;
        if (modrm.mod == 0b01) {
            m.displacement = sign_extend8(in.next8());
            m.displacement_size = 1;
        } else if (modrm.mod == 0b10) {
            m.displacement = in.next16();
            m.displacement_size = 2;
        }
    }
    m.segment = data_segment(m, override);
    return m;
}

const GroupTable* group_table(Mnemonic m) noexcept {
    switch (m) {
    case Mnemonic::Group1: return &kGroup1;
    case Mnemonic::Group82: return &kGroup82;
    case Mnemonic::Group3: return &kGroup3;
    case Mnemonic::Group4: return &kGroup4;
    case Mnemonic::Group5: return &kGroup5;
    case Mnemonic::Group11: return &kGroup11;
    default: return nullptr;
    }
}

bool uses_modrm(OperandKind kind) noexcept {
    using enum OperandKind;
    return kind == Eb || kind == Ev || kind == Ew || kind == Gb || kind == Gv || kind == Sw;
}

Width width_of(OperandKind kind) noexcept {
    using enum OperandKind;
    switch (kind) {
    case Ev: case Ew: case Gv: case Sw: case Iv: case Ibs: case Zv: case AX: case Ov: case Jv:
        return Width::Word;
    default:
        return Width::Byte;
    }
}

Operand register_operand(Width w, std::uint8_t index) noexcept {
    return {w == Width::Byte ? Operand::Type::Reg8 : Operand::Type::Reg16, index, 0, {}};
}

Operand immediate_operand(std::uint16_t value) noexcept {
    return {Operand::Type::Imm, 0, value, {}};
}

Operand memory_operand(const MemoryRef& m) noexcept {
    return {Operand::Type::Mem, 0, 0, m};
}

// Destinazione di un salto relativo: lo spiazzamento si somma all'IP dell'istruzione
// successiva, con wrap a 16 bit. Lo spiazzamento è l'ultimo campo dell'istruzione, quindi
// subito dopo averlo letto i byte letti coincidono con la lunghezza totale.
Operand target_operand(std::uint16_t ip, const ByteReader& in, std::uint16_t displacement) noexcept {
    const auto next_ip = static_cast<std::uint16_t>(ip + in.count());
    return {Operand::Type::Target, 0, static_cast<std::uint16_t>(next_ip + displacement), {}};
}

// Dati già letti quando si interpretano gli operandi.
struct DecodeContext {
    std::uint16_t ip = 0;  // inizio dell'istruzione, prefissi compresi
    std::uint8_t opcode = 0;
    ModRM modrm;
    MemoryRef rm_memory;  // valido se modrm.mod != 11
    std::optional<SegReg> segment_override;
};

// Interpreta un descrittore di operando della tabella. Lo spiazzamento del ModR/M è già
// stato letto; qui si leggono solo gli immediati e gli indirizzi diretti (A0-A3), che
// nell'istruzione vengono dopo, quindi l'ordine dei byte è rispettato.
// Restituisce nullopt per le forme fuori dal sottoinsieme.
std::optional<Operand> decode_operand(OperandKind kind, const DecodeContext& ctx,
                                      ByteReader& in) noexcept {
    using enum OperandKind;
    switch (kind) {
    case None:
        return Operand{};
    case Eb:
    case Ev:
    case Ew:
        if (ctx.modrm.mod != 0b11) {
            return memory_operand(ctx.rm_memory);
        }
        return register_operand(width_of(kind), ctx.modrm.rm);
    case Gb:
    case Gv:
        return register_operand(width_of(kind), ctx.modrm.reg);
    case Sw:
        if (ctx.modrm.reg > 3) {
            // "mod 1-- r/m": not used (Tabella 4-13). Disallineamento con l'hardware:
            // l'8086 reale usa solo i due bit bassi (reg & 3). Come da documentazione.
            return std::nullopt;
        }
        return Operand{Operand::Type::Seg, ctx.modrm.reg, 0, {}};
    case Zb:
    case Zv:
        return register_operand(width_of(kind), static_cast<std::uint8_t>(ctx.opcode & 7));
    case AL:
        return register_operand(Width::Byte, 0);
    case AX:
        return register_operand(Width::Word, 0);
    case Ib:
        return immediate_operand(in.next8());
    case Iv:
        return immediate_operand(in.next16());
    case Ibs:
        return immediate_operand(sign_extend8(in.next8()));
    case Ob:
    case Ov: {
        MemoryRef m;
        m.displacement = in.next16();
        m.displacement_size = 2;
        m.segment = data_segment(m, ctx.segment_override);
        return memory_operand(m);
    }
    case Jb: {
        const std::uint16_t displacement = sign_extend8(in.next8());
        return target_operand(ctx.ip, in, displacement);
    }
    case Jv: {
        const std::uint16_t displacement = in.next16();
        return target_operand(ctx.ip, in, displacement);
    }
    }
    return std::nullopt;
}

}  // namespace

Instruction decode(const Memory& memory, std::uint16_t cs, std::uint16_t ip) {
    Instruction ins;
    ins.cs = cs;
    ins.ip = ip;
    ByteReader in(memory, cs, ip);

    // Prefissi di segment override (001 sr 110): se ce n'è più d'uno, vale l'ultimo.
    ins.opcode = in.next8();
    std::uint8_t prefixes = 0;
    while (kOpcodeTable[ins.opcode].mnemonic == Mnemonic::SegmentPrefix) {
        if (++prefixes > kMaxPrefixes) {
            ins.length = in.count();
            return ins;  // Unimplemented, con il prefisso come opcode
        }
        ins.segment_override = static_cast<SegReg>((ins.opcode >> 3) & 3);
        ins.opcode = in.next8();
    }

    const OpcodeInfo& info = kOpcodeTable[ins.opcode];
    ins.mnemonic = info.mnemonic;

    DecodeContext ctx;
    ctx.ip = ip;
    ctx.opcode = ins.opcode;
    ctx.segment_override = ins.segment_override;

    // Ordine dei byte (manuale, pagina 4-20): opcode, ModR/M, spiazzamento, immediato.
    const GroupTable* group = group_table(info.mnemonic);
    if (group != nullptr || uses_modrm(info.dst) || uses_modrm(info.src)) {
        ctx.modrm = split_modrm(in.next8());
        if (ctx.modrm.mod != 0b11) {
            ctx.rm_memory = decode_memory(ctx.modrm, in, ins.segment_override);
        }
    }
    if (group != nullptr) {
        ins.group_reg = ctx.modrm.reg;
        ins.mnemonic = (*group)[ctx.modrm.reg];
    }

    if (ins.mnemonic != Mnemonic::Unimplemented) {
        const auto dst = decode_operand(info.dst, ctx, in);
        const auto src = decode_operand(info.src, ctx, in);
        // MOV CS, r/m16 (8E /1) cambierebbe CS senza cambiare IP: di fatto un salto far,
        // fuori dal sottoinsieme. La suite non lo prova.
        const bool loads_cs = dst && dst->type == Operand::Type::Seg &&
                              dst->reg == static_cast<std::uint8_t>(SegReg::CS);
        if (dst && src && !loads_cs) {
            ins.dst = *dst;
            ins.src = *src;
            ins.dst_kind = info.dst;
            ins.src_kind = info.src;
            ins.width = width_of(info.dst != OperandKind::None ? info.dst : info.src);
        } else {
            ins.mnemonic = Mnemonic::Unimplemented;
        }
    }

    ins.length = in.count();
    return ins;
}

}  // namespace emu
