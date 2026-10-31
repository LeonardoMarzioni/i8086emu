#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/memory.hpp"

using namespace emu;

TEST_CASE("indirizzo fisico: segmento * 16 + offset, ridotto a 20 bit", "[memory]") {
    CHECK(physical_address(0x0000, 0x0000) == 0x00000);
    CHECK(physical_address(0x1000, 0x0100) == 0x10100);
    CHECK(physical_address(0x1234, 0x5678) == 0x179B8);
    // Oltre il megabyte si riparte da 0: l'8086 ha solo 20 linee di indirizzo.
    CHECK(physical_address(0xFFFF, 0x0010) == 0x00000);
    CHECK(physical_address(0xFFFF, 0xFFFF) == 0x0FFEF);
}

TEST_CASE("segmenti diversi possono indicare lo stesso byte fisico", "[memory]") {
    Memory mem;
    mem.write8(0x1000, 0x0010, 0x5A);
    CHECK(mem.read8(0x1001, 0x0000) == 0x5A);
}

TEST_CASE("le parole sono little-endian", "[memory]") {
    Memory mem;
    mem.write16(0x1000, 0x0200, 0x1234);
    CHECK(mem.peek(0x10200) == 0x34);
    CHECK(mem.peek(0x10201) == 0x12);
    CHECK(mem.read16(0x1000, 0x0200) == 0x1234);
    CHECK(mem.read8(0x1000, 0x0201) == 0x12);
}

TEST_CASE("una parola all'offset FFFF fa wrap dentro il segmento", "[memory]") {
    Memory mem;
    mem.write16(0x1000, 0xFFFF, 0xABCD);
    CHECK(mem.peek(0x1FFFF) == 0xCD);  // 1000:FFFF
    CHECK(mem.peek(0x10000) == 0xAB);  // 1000:0000
    CHECK(mem.peek(0x20000) == 0x00);  // il byte fisico successivo resta intatto
    CHECK(mem.read16(0x1000, 0xFFFF) == 0xABCD);
}

TEST_CASE("load copia i byte e oltre il megabyte riparte da 0", "[memory]") {
    Memory mem;
    const std::array<std::uint8_t, 3> bytes{0x11, 0x22, 0x33};
    mem.load(0xFFFFE, bytes);
    CHECK(mem.peek(0xFFFFE) == 0x11);
    CHECK(mem.peek(0xFFFFF) == 0x22);
    CHECK(mem.peek(0x00000) == 0x33);
}

TEST_CASE("peek e poke riducono l'indirizzo a 20 bit", "[memory]") {
    Memory mem;
    mem.poke(0x100005, 0x77);
    CHECK(mem.peek(0x00005) == 0x77);
}

TEST_CASE("clear azzera la memoria", "[memory]") {
    Memory mem;
    mem.poke(0x12345, 0xFF);
    mem.clear();
    CHECK(mem.peek(0x12345) == 0x00);
}
