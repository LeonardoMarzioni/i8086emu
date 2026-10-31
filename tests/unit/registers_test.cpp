#include <array>
#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/registers.hpp"

using namespace emu;

TEST_CASE("AL e AH sono le due metà di AX", "[registers]") {
    Registers r;
    r.set(Reg16::AX, 0x1234);
    CHECK(r.get(Reg8::AL) == 0x34);
    CHECK(r.get(Reg8::AH) == 0x12);

    r.set(Reg8::AH, 0xAB);
    CHECK(r.get(Reg16::AX) == 0xAB34);
    r.set(Reg8::AL, 0xCD);
    CHECK(r.get(Reg16::AX) == 0xABCD);
}

TEST_CASE("codifica a 8 bit: AL CL DL BL AH CH DH BH = 0..7", "[registers]") {
    Registers r;
    r.set(Reg16::AX, 0x0100);
    r.set(Reg16::CX, 0x0302);
    r.set(Reg16::DX, 0x0504);
    r.set(Reg16::BX, 0x0706);
    const std::array<std::uint8_t, 8> expected{0x00, 0x02, 0x04, 0x06, 0x01, 0x03, 0x05, 0x07};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("indice " << i);
        CHECK(r.get(static_cast<Reg8>(i)) == expected[i]);
    }
}

TEST_CASE("scrivere una metà non tocca l'altra né gli altri registri", "[registers]") {
    Registers r;
    r.set(Reg16::BX, 0xFFFF);
    r.set(Reg8::BH, 0x00);
    CHECK(r.get(Reg16::BX) == 0x00FF);
    r.set(Reg8::BL, 0x00);
    r.set(Reg8::BH, 0x99);
    CHECK(r.get(Reg16::BX) == 0x9900);
    CHECK(r.get(Reg16::AX) == 0);
    CHECK(r.get(Reg16::SP) == 0);
}

TEST_CASE("registri a 16 bit e di segmento sono indipendenti", "[registers]") {
    Registers r;
    r.set(Reg16::SP, 0xFFFE);
    r.set(Reg16::BP, 0x1111);
    r.set(Reg16::SI, 0x2222);
    r.set(Reg16::DI, 0x3333);
    r.set(SegReg::ES, 0x4000);
    r.set(SegReg::CS, 0x1000);
    r.set(SegReg::SS, 0x2000);
    r.set(SegReg::DS, 0x3000);
    CHECK(r.get(Reg16::SP) == 0xFFFE);
    CHECK(r.get(Reg16::BP) == 0x1111);
    CHECK(r.get(Reg16::SI) == 0x2222);
    CHECK(r.get(Reg16::DI) == 0x3333);
    CHECK(r.get(SegReg::ES) == 0x4000);
    CHECK(r.get(SegReg::CS) == 0x1000);
    CHECK(r.get(SegReg::SS) == 0x2000);
    CHECK(r.get(SegReg::DS) == 0x3000);
}

TEST_CASE("confronto tra stati completi", "[registers]") {
    Registers a;
    Registers b;
    CHECK(a == b);
    b.ip = 1;
    CHECK(a != b);
    b = a;
    b.flags.set(Flag::CF, true);
    CHECK(a != b);
}
