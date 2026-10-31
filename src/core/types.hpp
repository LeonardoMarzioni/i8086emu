#pragma once

#include <cstdint>

namespace emu {

// Larghezza di un operando: corrisponde al bit "w" dell'opcode.
enum class Width : std::uint8_t { Byte, Word };

// Bit significativi per la larghezza data.
constexpr std::uint16_t width_mask(Width w) noexcept {
    return w == Width::Byte ? 0x00FF : 0xFFFF;
}

// Bit di segno per la larghezza data.
constexpr std::uint16_t sign_bit(Width w) noexcept {
    return w == Width::Byte ? 0x0080 : 0x8000;
}

}  // namespace emu
