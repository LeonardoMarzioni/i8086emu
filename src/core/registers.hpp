#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/flags.hpp"

namespace emu {

// L'ordine degli enumeratori coincide con la codifica nei campi reg e r/m del ModR/M:
// il decoder converte il campo con un semplice cast.
enum class Reg16 : std::uint8_t { AX, CX, DX, BX, SP, BP, SI, DI };
enum class Reg8 : std::uint8_t { AL, CL, DL, BL, AH, CH, DH, BH };
enum class SegReg : std::uint8_t { ES, CS, SS, DS };

class Registers {
public:
    constexpr std::uint16_t get(Reg16 r) const noexcept { return gpr_[index(r)]; }
    constexpr void set(Reg16 r, std::uint16_t value) noexcept { gpr_[index(r)] = value; }

    // Registri a 8 bit: i bit 1..0 dell'indice scelgono il registro a 16 bit (AX, CX, DX, BX),
    // il bit 2 sceglie la metà (0 = bassa, 1 = alta). Ad esempio AH = 4 = metà alta di AX.
    // Si usano shift e maschere: niente union (type punning indefinito in C++) e nessuna
    // dipendenza dall'endianness della macchina che esegue l'emulatore.
    constexpr std::uint8_t get(Reg8 r) const noexcept {
        const std::uint16_t word = gpr_[index(r) & 3u];
        return static_cast<std::uint8_t>(is_high(r) ? word >> 8 : word);
    }

    constexpr void set(Reg8 r, std::uint8_t value) noexcept {
        std::uint16_t& word = gpr_[index(r) & 3u];
        word = is_high(r) ? static_cast<std::uint16_t>((word & 0x00FF) | (value << 8))
                          : static_cast<std::uint16_t>((word & 0xFF00) | value);
    }

    constexpr std::uint16_t get(SegReg s) const noexcept { return seg_[index(s)]; }
    constexpr void set(SegReg s, std::uint16_t value) noexcept { seg_[index(s)] = value; }

    bool operator==(const Registers&) const = default;

    std::uint16_t ip = 0;
    Flags flags;

private:
    template <typename E>
    static constexpr std::size_t index(E e) noexcept {
        return static_cast<std::size_t>(e);
    }

    static constexpr bool is_high(Reg8 r) noexcept { return (index(r) & 4u) != 0; }

    std::array<std::uint16_t, 8> gpr_{};  // AX, CX, DX, BX, SP, BP, SI, DI
    std::array<std::uint16_t, 4> seg_{};  // ES, CS, SS, DS
};

}  // namespace emu
