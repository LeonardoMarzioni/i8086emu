#include "debugger/parse.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <string>

namespace emu::dbg {
namespace {

struct NamedRegister {
    std::string_view name;
    RegisterName reg;
};

const std::array<NamedRegister, 31> kRegisters = {{
    {"ax", Reg16::AX}, {"cx", Reg16::CX}, {"dx", Reg16::DX}, {"bx", Reg16::BX},
    {"sp", Reg16::SP}, {"bp", Reg16::BP}, {"si", Reg16::SI}, {"di", Reg16::DI},
    {"al", Reg8::AL},  {"cl", Reg8::CL},  {"dl", Reg8::DL},  {"bl", Reg8::BL},
    {"ah", Reg8::AH},  {"ch", Reg8::CH},  {"dh", Reg8::DH},  {"bh", Reg8::BH},
    {"es", SegReg::ES}, {"cs", SegReg::CS}, {"ss", SegReg::SS}, {"ds", SegReg::DS},
    {"ip", InstructionPointer{}}, {"flags", FlagsRegister{}},
    {"cf", Flag::CF}, {"pf", Flag::PF}, {"af", Flag::AF}, {"zf", Flag::ZF}, {"sf", Flag::SF},
    {"tf", Flag::TF}, {"if", Flag::IF}, {"df", Flag::DF}, {"of", Flag::OF},
}};

std::string to_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Valore di un registro a 16 bit nominato come offset di un indirizzo.
std::optional<std::uint16_t> register_value(const RegisterName& reg, const Registers& regs) {
    if (const auto* r16 = std::get_if<Reg16>(&reg)) {
        return regs.get(*r16);
    }
    if (std::holds_alternative<InstructionPointer>(reg)) {
        return regs.ip;
    }
    return std::nullopt;
}

}  // namespace

std::vector<std::string_view> split_words(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) == 0) {
            ++pos;
        }
        if (pos > start) {
            words.push_back(line.substr(start, pos - start));
        }
    }
    return words;
}

std::optional<std::uint16_t> parse_hex16(std::string_view text) {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size() || value > 0xFFFF) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

std::optional<std::uint64_t> parse_count(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<RegisterName> parse_register(std::string_view name) {
    if (name.empty()) {
        return std::nullopt;
    }
    const std::string lower = to_lower(name);
    for (const NamedRegister& r : kRegisters) {
        if (r.name == lower) {
            return r.reg;
        }
    }
    return std::nullopt;
}

std::optional<Address> parse_address(std::string_view text, const Registers& regs,
                                     SegReg default_segment) {
    Address address{regs.get(default_segment), 0};
    std::string_view offset_text = text;

    if (const std::size_t colon = text.find(':'); colon != std::string_view::npos) {
        const std::string_view segment_text = text.substr(0, colon);
        offset_text = text.substr(colon + 1);
        const auto reg = parse_register(segment_text);
        if (reg && std::holds_alternative<SegReg>(*reg)) {
            address.segment = regs.get(std::get<SegReg>(*reg));
        } else if (const auto value = parse_hex16(segment_text)) {
            address.segment = *value;
        } else {
            return std::nullopt;
        }
    }

    // Prima i registri a 16 bit e ip; altrimenti un numero. Così "cf" o "df", che sono
    // nomi di flag ma anche numeri esadecimali validi, come offset valgono come numeri.
    const auto reg = parse_register(offset_text);
    if (const auto reg_offset = reg ? register_value(*reg, regs) : std::nullopt) {
        address.offset = *reg_offset;
    } else if (const auto number = parse_hex16(offset_text)) {
        address.offset = *number;
    } else {
        return std::nullopt;
    }
    return address;
}

}  // namespace emu::dbg
