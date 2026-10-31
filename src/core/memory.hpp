#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace emu {

inline constexpr std::uint32_t kMemorySize = 0x10'0000;   // 1 MB
inline constexpr std::uint32_t kAddressMask = 0x0F'FFFF;  // 20 linee di indirizzo

// Indirizzo fisico a 20 bit: segmento * 16 + offset, con wrap a 1 MB come sull'8086.
constexpr std::uint32_t physical_address(std::uint16_t segment, std::uint16_t offset) noexcept {
    return ((std::uint32_t{segment} << 4) + offset) & kAddressMask;
}

// Memoria da 1 MB, little-endian.
// I byte stanno su heap: un array da 1 MB dentro l'oggetto finirebbe sullo stack
// ogni volta che una Memory (o un oggetto che la contiene) è una variabile locale,
// e su Windows lo stack predefinito è proprio di 1 MB.
class Memory {
public:
    Memory();

    // Accessi della CPU, per segmento:offset. Una parola è little-endian e il suo byte alto
    // sta all'offset successivo con wrap a 16 bit dentro lo stesso segmento: una parola
    // all'offset FFFFh prende il byte alto da segmento:0000, non dal byte fisico seguente.
    std::uint8_t read8(std::uint16_t segment, std::uint16_t offset) const noexcept;
    std::uint16_t read16(std::uint16_t segment, std::uint16_t offset) const noexcept;
    void write8(std::uint16_t segment, std::uint16_t offset, std::uint8_t value) noexcept;
    void write16(std::uint16_t segment, std::uint16_t offset, std::uint16_t value) noexcept;

    // Accessi per indirizzo fisico, fuori dal bus della CPU: caricatore (load), harness, test.
    // L'indirizzo viene ridotto a 20 bit, quindi nessun accesso può uscire dall'array.
    std::uint8_t peek(std::uint32_t address) const noexcept;
    void poke(std::uint32_t address, std::uint8_t value) noexcept;

    // Copia i byte a partire da address; oltre la fine del megabyte riparte da 0.
    void load(std::uint32_t address, std::span<const std::uint8_t> bytes) noexcept;

    // Azzera tutta la memoria.
    void clear() noexcept;

private:
    std::vector<std::uint8_t> bytes_;
};

}  // namespace emu
