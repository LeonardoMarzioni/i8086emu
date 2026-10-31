#pragma once

#include <cstdint>

namespace emu {

// Posizione di ciascun flag nel registro FLAGS dell'8086.
enum class Flag : std::uint16_t {
    CF = 1u << 0,   // carry / borrow
    PF = 1u << 2,   // parità del byte basso del risultato (1 se pari)
    AF = 1u << 4,   // carry / borrow dal bit 3 al bit 4
    ZF = 1u << 6,
    SF = 1u << 7,
    TF = 1u << 8,   // memorizzato, senza effetto
    IF = 1u << 9,   // memorizzato, senza effetto
    DF = 1u << 10,  // memorizzato, senza effetto
    OF = 1u << 11,
};

// Registro FLAGS conservato come parola a 16 bit con lo stesso layout dell'hardware:
// debugger, harness dei test e (in futuro) PUSHF lavorano direttamente sulla parola.
class Flags {
public:
    // I nove flag definiti.
    static constexpr std::uint16_t kDefined = 0x0FD5;
    // Bit 1 e bit 12..15: sull'8086 si leggono sempre a 1 (i bit 3 e 5 sempre a 0).
    static constexpr std::uint16_t kForcedOnes = 0xF002;

    constexpr bool test(Flag f) const noexcept {
        return (bits_ & static_cast<std::uint16_t>(f)) != 0;
    }

    constexpr void set(Flag f, bool on) noexcept {
        const auto mask = static_cast<std::uint16_t>(f);
        bits_ = on ? static_cast<std::uint16_t>(bits_ | mask)
                   : static_cast<std::uint16_t>(bits_ & ~mask);
    }

    // Valore del registro come lo vede l'8086, con i bit riservati al loro valore fisso.
    constexpr std::uint16_t word() const noexcept {
        return static_cast<std::uint16_t>(bits_ | kForcedOnes);
    }

    // Carica il registro da una parola: i bit riservati vengono ignorati.
    constexpr void set_word(std::uint16_t w) noexcept {
        bits_ = static_cast<std::uint16_t>(w & kDefined);
    }

    bool operator==(const Flags&) const = default;

private:
    std::uint16_t bits_ = 0;  // contiene solo bit di kDefined
};

}  // namespace emu
