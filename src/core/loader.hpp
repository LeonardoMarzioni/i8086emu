#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "core/cpu.hpp"
#include "core/memory.hpp"

namespace emu {

// Stato iniziale di un programma in stile .COM: un solo segmento per codice, dati e stack.
struct LoadConfig {
    std::uint16_t segment = 0x1000;  // CS = DS = ES = SS
    std::uint16_t ip = 0x0100;       // il programma viene copiato a segment:ip
    std::uint16_t sp = 0xFFFE;
};

// Dimensione massima di un programma: dal punto di caricamento alla fine del segmento.
constexpr std::size_t max_program_size(const LoadConfig& config) noexcept {
    return std::size_t{0x10000} - config.ip;
}

// Prepara memoria e registri come per un programma .COM: memoria azzerata, programma copiato
// a segment:ip, tutti i segmenti uguali, SP come da configurazione, gli altri registri a zero.
// Restituisce false, senza modificare nulla, se il programma non entra nel segmento.
//
// Non c'è un PSP con INT 20h a segment:0000: un RET finale salta lì, dove i byte 00 00 sono
// ADD [BX+SI],AL. Per convenzione i programmi terminano con HLT.
[[nodiscard]] bool load_program(Memory& memory, Cpu& cpu, std::span<const std::uint8_t> program,
                                const LoadConfig& config = {});

}  // namespace emu
