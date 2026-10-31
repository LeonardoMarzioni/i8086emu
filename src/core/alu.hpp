#pragma once

#include <cstdint>

#include "core/flags.hpp"
#include "core/types.hpp"

namespace emu {

// Operazioni ALU a due operandi. L'ordine coincide con i bit 5..3 degli opcode 00..3F
// e con il campo reg dei gruppi 80/81/83: l'8086 usa la stessa numerazione nei due posti.
// ADC e SBB sono fuori dal sottoinsieme: restano qui solo per non rompere la numerazione,
// e la tabella degli opcode non le produce mai.
enum class AluOp : std::uint8_t { Add, Or, Adc, Sbb, And, Sub, Xor, Cmp };

// Funzioni pure: considerano solo i bit della larghezza w, aggiornano i flag
// e restituiscono il risultato troncato alla larghezza.

// Per Cmp calcola la sottrazione: è il chiamante a non scrivere il risultato.
std::uint16_t alu_binary(AluOp op, Width w, std::uint16_t a, std::uint16_t b, Flags& f) noexcept;

// Come ADD a,1 e SUB a,1, ma CF non viene toccato.
std::uint16_t alu_inc(Width w, std::uint16_t a, Flags& f) noexcept;
std::uint16_t alu_dec(Width w, std::uint16_t a, Flags& f) noexcept;

// NEG è SUB 0,a: CF = (a != 0) è semplicemente il prestito della sottrazione.
std::uint16_t alu_neg(Width w, std::uint16_t a, Flags& f) noexcept;

// NOT non modifica i flag: per questo non riceve Flags.
std::uint16_t alu_not(Width w, std::uint16_t a) noexcept;

}  // namespace emu
