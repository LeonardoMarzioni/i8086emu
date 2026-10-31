#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "core/flags.hpp"
#include "core/registers.hpp"

namespace emu::dbg {

// Indirizzo logico segmento:offset, come lo scrive e lo legge il programmatore.
struct Address {
    std::uint16_t segment = 0;
    std::uint16_t offset = 0;

    bool operator==(const Address&) const = default;
};

// Registri nominabili nei comandi: quelli dei tre enum del core, più IP, FLAGS e i singoli flag.
struct InstructionPointer {
    bool operator==(const InstructionPointer&) const = default;
};
struct FlagsRegister {
    bool operator==(const FlagsRegister&) const = default;
};
using RegisterName = std::variant<Reg16, Reg8, SegReg, InstructionPointer, FlagsRegister, Flag>;

// Convenzioni dei numeri: indirizzi e valori sono esadecimali (prefisso 0x facoltativo),
// i conteggi (step n, x ... n, disasm n) sono decimali.

// Parole separate da spazi.
std::vector<std::string_view> split_words(std::string_view line);

// Numero esadecimale a 16 bit, ad esempio "1A2B" o "0x100".
std::optional<std::uint16_t> parse_hex16(std::string_view text);

// Conteggio decimale, ad esempio "10".
std::optional<std::uint64_t> parse_count(std::string_view text);

// Nome di registro o di flag, senza distinzione tra maiuscole e minuscole.
std::optional<RegisterName> parse_register(std::string_view name);

// Indirizzo "seg:off" oppure "off". Il segmento può essere un numero o es/cs/ss/ds,
// l'offset un numero, un registro a 16 bit o ip. Senza segmento si usa default_segment.
std::optional<Address> parse_address(std::string_view text, const Registers& regs,
                                     SegReg default_segment);

}  // namespace emu::dbg
