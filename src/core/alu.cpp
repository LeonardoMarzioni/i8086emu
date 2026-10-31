#include "core/alu.hpp"

#include <bit>
#include <cassert>

namespace emu {
namespace {

std::uint16_t truncate(Width w, std::uint16_t value) noexcept {
    return static_cast<std::uint16_t>(value & width_mask(w));
}

// PF vale 1 se il byte basso del risultato ha un numero pari di bit a 1,
// anche nelle operazioni a 16 bit.
bool parity_even(std::uint16_t result) noexcept {
    return std::popcount(static_cast<std::uint8_t>(result)) % 2 == 0;
}

// ZF, SF e PF dipendono solo dal risultato: sono comuni a tutte le operazioni.
void set_szp(Flags& f, Width w, std::uint16_t result) noexcept {
    f.set(Flag::ZF, result == 0);
    f.set(Flag::SF, (result & sign_bit(w)) != 0);
    f.set(Flag::PF, parity_even(result));
}

// AF: riporto o prestito dal bit 3 al bit 4. Il bit 4 di a ^ b ^ r vale 1 esattamente
// quando nel bit 4 è entrato un riporto (o è uscito un prestito).
bool aux_carry(std::uint16_t a, std::uint16_t b, std::uint16_t result) noexcept {
    return ((a ^ b ^ result) & 0x10) != 0;
}

std::uint16_t add(Width w, std::uint16_t a, std::uint16_t b, Flags& f, bool update_cf) noexcept {
    const std::uint32_t full = std::uint32_t{a} + b;
    const auto result = static_cast<std::uint16_t>(full & width_mask(w));
    if (update_cf) {
        f.set(Flag::CF, full > width_mask(w));
    }
    // Overflow: a e b hanno lo stesso segno e il risultato ha segno diverso da entrambi.
    f.set(Flag::OF, ((a ^ result) & (b ^ result) & sign_bit(w)) != 0);
    f.set(Flag::AF, aux_carry(a, b, result));
    set_szp(f, w, result);
    return result;
}

std::uint16_t sub(Width w, std::uint16_t a, std::uint16_t b, Flags& f, bool update_cf) noexcept {
    const auto result = static_cast<std::uint16_t>((a - b) & width_mask(w));
    if (update_cf) {
        f.set(Flag::CF, a < b);  // prestito
    }
    // Overflow: a e b hanno segno diverso e il risultato ha segno diverso da a.
    f.set(Flag::OF, ((a ^ b) & (a ^ result) & sign_bit(w)) != 0);
    f.set(Flag::AF, aux_carry(a, b, result));
    set_szp(f, w, result);
    return result;
}

std::uint16_t logic(Width w, std::uint16_t result, Flags& f) noexcept {
    f.set(Flag::CF, false);
    f.set(Flag::OF, false);
    // AF è indefinito sull'8086 per AND/OR/XOR (la suite lo maschera): lo azzeriamo.
    f.set(Flag::AF, false);
    set_szp(f, w, result);
    return result;
}

}  // namespace

std::uint16_t alu_binary(AluOp op, Width w, std::uint16_t a, std::uint16_t b, Flags& f) noexcept {
    a = truncate(w, a);
    b = truncate(w, b);
    switch (op) {
    case AluOp::Add:
        return add(w, a, b, f, true);
    case AluOp::Or:
        return logic(w, static_cast<std::uint16_t>(a | b), f);
    case AluOp::And:
        return logic(w, static_cast<std::uint16_t>(a & b), f);
    case AluOp::Xor:
        return logic(w, static_cast<std::uint16_t>(a ^ b), f);
    case AluOp::Sub:
    case AluOp::Cmp:
        return sub(w, a, b, f, true);
    case AluOp::Adc:
    case AluOp::Sbb:
        break;
    }
    assert(false && "ADC/SBB fuori dal sottoinsieme");
    return a;
}

std::uint16_t alu_inc(Width w, std::uint16_t a, Flags& f) noexcept {
    return add(w, truncate(w, a), 1, f, false);
}

std::uint16_t alu_dec(Width w, std::uint16_t a, Flags& f) noexcept {
    return sub(w, truncate(w, a), 1, f, false);
}

std::uint16_t alu_neg(Width w, std::uint16_t a, Flags& f) noexcept {
    return sub(w, 0, truncate(w, a), f, true);
}

std::uint16_t alu_not(Width w, std::uint16_t a) noexcept {
    return truncate(w, static_cast<std::uint16_t>(~a));
}

}  // namespace emu
