#include "core/memory.hpp"

#include <algorithm>

namespace emu {

Memory::Memory() : bytes_(kMemorySize, 0) {}

std::uint8_t Memory::read8(std::uint16_t segment, std::uint16_t offset) const noexcept {
    return bytes_[physical_address(segment, offset)];
}

std::uint16_t Memory::read16(std::uint16_t segment, std::uint16_t offset) const noexcept {
    const std::uint8_t low = read8(segment, offset);
    const std::uint8_t high = read8(segment, static_cast<std::uint16_t>(offset + 1));
    return static_cast<std::uint16_t>(low | (high << 8));
}

void Memory::write8(std::uint16_t segment, std::uint16_t offset, std::uint8_t value) noexcept {
    bytes_[physical_address(segment, offset)] = value;
}

void Memory::write16(std::uint16_t segment, std::uint16_t offset, std::uint16_t value) noexcept {
    write8(segment, offset, static_cast<std::uint8_t>(value));
    write8(segment, static_cast<std::uint16_t>(offset + 1), static_cast<std::uint8_t>(value >> 8));
}

std::uint8_t Memory::peek(std::uint32_t address) const noexcept {
    return bytes_[address & kAddressMask];
}

void Memory::poke(std::uint32_t address, std::uint8_t value) noexcept {
    bytes_[address & kAddressMask] = value;
}

void Memory::load(std::uint32_t address, std::span<const std::uint8_t> bytes) noexcept {
    for (const std::uint8_t b : bytes) {
        poke(address++, b);
    }
}

void Memory::clear() noexcept {
    std::fill(bytes_.begin(), bytes_.end(), std::uint8_t{0});
}

}  // namespace emu
