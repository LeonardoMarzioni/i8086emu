#include "core/disassembler.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <string_view>

namespace emu {
namespace {

constexpr std::array<std::string_view, 8> kReg16Names = {"ax", "cx", "dx", "bx",
                                                         "sp", "bp", "si", "di"};
constexpr std::array<std::string_view, 8> kReg8Names = {"al", "cl", "dl", "bl",
                                                        "ah", "ch", "dh", "bh"};
constexpr std::array<std::string_view, 4> kSegNames = {"es", "cs", "ss", "ds"};

constexpr std::array<std::string_view, kMnemonicCount> make_mnemonic_names() {
    std::array<std::string_view, kMnemonicCount> names{};
    auto set = [&names](Mnemonic m, std::string_view name) {
        names[static_cast<std::size_t>(m)] = name;
    };
    set(Mnemonic::Mov, "mov");
    set(Mnemonic::Add, "add");
    set(Mnemonic::Or, "or");
    set(Mnemonic::And, "and");
    set(Mnemonic::Sub, "sub");
    set(Mnemonic::Xor, "xor");
    set(Mnemonic::Cmp, "cmp");
    set(Mnemonic::Inc, "inc");
    set(Mnemonic::Dec, "dec");
    set(Mnemonic::Neg, "neg");
    set(Mnemonic::Not, "not");
    set(Mnemonic::Nop, "nop");
    set(Mnemonic::Hlt, "hlt");
    set(Mnemonic::Push, "push");
    set(Mnemonic::Pop, "pop");
    set(Mnemonic::Jmp, "jmp");
    set(Mnemonic::Jz, "jz");
    set(Mnemonic::Jnz, "jnz");
    set(Mnemonic::Call, "call");
    set(Mnemonic::Ret, "ret");
    return names;
}

constexpr auto kMnemonicNames = make_mnemonic_names();

template <typename E>
constexpr std::size_t index(E e) noexcept {
    return static_cast<std::size_t>(e);
}

std::string hex(std::uint16_t value) {
    return std::format("0x{:x}", value);
}

// Valore con segno esplicito, come lo scrive ndisasm: "+0x5", "-0x2".
std::string signed_hex(int value) {
    return value < 0 ? std::format("-0x{:x}", -value) : std::format("+0x{:x}", value);
}

std::string signed_hex8(std::uint16_t value) {
    return signed_hex(static_cast<std::int8_t>(static_cast<std::uint8_t>(value)));
}

std::string signed_hex16(std::uint16_t value) {
    return signed_hex(static_cast<std::int16_t>(value));
}

std::string memory_text(const MemoryRef& m, const std::optional<SegReg>& override) {
    std::string text = "[";
    if (override) {
        text += kSegNames[index(*override)];
        text += ':';
    }
    std::string_view separator;
    if (m.base) {
        text += kReg16Names[index(*m.base)];
        separator = "+";
    }
    if (m.index) {
        text += separator;
        text += kReg16Names[index(*m.index)];
        separator = "+";
    }
    // Indirizzo diretto senza segno; spiazzamento dopo un registro con segno, anche a 16 bit
    // (ndisasm scrive [bx-0x2] per FFFEh: il risultato a 16 bit è lo stesso).
    if (!m.base && !m.index) {
        text += hex(m.displacement);
    } else if (m.displacement_size == 1) {
        text += signed_hex8(m.displacement);
    } else if (m.displacement_size == 2) {
        text += signed_hex16(m.displacement);
    }
    text += ']';
    return text;
}

bool is_register(const Operand& op) noexcept {
    return op.type == Operand::Type::Reg8 || op.type == Operand::Type::Reg16 ||
           op.type == Operand::Type::Seg;
}

std::string operand_text(const Operand& op, OperandKind kind, const Instruction& ins) {
    switch (op.type) {
    case Operand::Type::Reg8:
        return std::string(kReg8Names[op.reg]);
    case Operand::Type::Reg16:
        return std::string(kReg16Names[op.reg]);
    case Operand::Type::Seg:
        return std::string(kSegNames[op.reg]);
    case Operand::Type::Imm:
        // 83: immediato a 8 bit esteso con segno, che ndisasm scrive "byte +0x5"
        return kind == OperandKind::Ibs ? "byte " + signed_hex8(op.imm) : hex(op.imm);
    case Operand::Type::Target:
        return hex(op.imm);
    case Operand::Type::Mem: {
        // La dimensione si scrive solo quando nessun registro la rende evidente. CALL e JMP
        // indirette near leggono sempre una parola: ndisasm non la scrive.
        const bool is_near_transfer = ins.mnemonic == Mnemonic::Call || ins.mnemonic == Mnemonic::Jmp;
        const bool needs_size = !is_register(ins.dst) && !is_register(ins.src) && !is_near_transfer;
        const std::string_view size =
            !needs_size ? "" : (ins.width == Width::Byte ? "byte " : "word ");
        return std::string(size) + memory_text(op.mem, ins.segment_override);
    }
    case Operand::Type::None:
        break;
    }
    return {};
}

}  // namespace

std::string format_instruction(const Instruction& ins) {
    if (ins.mnemonic == Mnemonic::Unimplemented) {
        return std::format("db 0x{:x}", ins.opcode);
    }

    std::string text;
    // Un prefisso senza operandi in memoria non compare tra le parentesi quadre:
    // ndisasm lo scrive prima dell'istruzione, ad esempio "es mov ax,0x1234".
    const bool has_memory = ins.dst.type == Operand::Type::Mem || ins.src.type == Operand::Type::Mem;
    if (ins.segment_override && !has_memory) {
        text += kSegNames[index(*ins.segment_override)];
        text += ' ';
    }

    text += kMnemonicNames[index(ins.mnemonic)];
    if (ins.mnemonic == Mnemonic::Jmp && ins.dst_kind == OperandKind::Jb) {
        text += " short";
    }
    if (ins.dst.type != Operand::Type::None) {
        text += ' ';
        text += operand_text(ins.dst, ins.dst_kind, ins);
    }
    if (ins.src.type != Operand::Type::None) {
        text += ',';
        text += operand_text(ins.src, ins.src_kind, ins);
    }
    return text;
}

DisassembledLine disassemble(const Memory& memory, std::uint16_t cs, std::uint16_t ip) {
    DisassembledLine line;
    line.instruction = decode(memory, cs, ip);
    std::uint8_t length = line.instruction.length;
    if (line.instruction.mnemonic == Mnemonic::Unimplemented) {
        length = 1;
        line.text = std::format("db 0x{:x}", memory.read8(cs, ip));
    } else {
        line.text = format_instruction(line.instruction);
    }
    for (std::uint8_t i = 0; i < length; ++i) {
        line.bytes.push_back(memory.read8(cs, static_cast<std::uint16_t>(ip + i)));
    }
    return line;
}

}  // namespace emu
