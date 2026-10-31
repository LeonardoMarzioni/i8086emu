#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debugger/parse.hpp"

using namespace emu;
using namespace emu::dbg;

TEST_CASE("parole separate da spazi", "[parse]") {
    const std::vector<std::string_view> words = split_words("  x   ds:100\t16 ");
    REQUIRE(words.size() == 3);
    CHECK(words[0] == "x");
    CHECK(words[1] == "ds:100");
    CHECK(words[2] == "16");
    CHECK(split_words("   ").empty());
}

TEST_CASE("numeri esadecimali a 16 bit", "[parse]") {
    CHECK(parse_hex16("100") == 0x100);
    CHECK(parse_hex16("0x1A2b") == 0x1A2B);
    CHECK(parse_hex16("FFFF") == 0xFFFF);
    CHECK_FALSE(parse_hex16("10000").has_value());
    CHECK_FALSE(parse_hex16("0x").has_value());
    CHECK_FALSE(parse_hex16("").has_value());
    CHECK_FALSE(parse_hex16("12g").has_value());
    CHECK_FALSE(parse_hex16("-1").has_value());
}

TEST_CASE("conteggi decimali", "[parse]") {
    CHECK(parse_count("10") == 10u);
    CHECK_FALSE(parse_count("0x10").has_value());
    CHECK_FALSE(parse_count("").has_value());
}

TEST_CASE("nomi di registro senza distinzione tra maiuscole e minuscole", "[parse]") {
    CHECK(parse_register("AX") == RegisterName{Reg16::AX});
    CHECK(parse_register("bh") == RegisterName{Reg8::BH});
    CHECK(parse_register("Ds") == RegisterName{SegReg::DS});
    CHECK(parse_register("ip") == RegisterName{InstructionPointer{}});
    CHECK(parse_register("flags") == RegisterName{FlagsRegister{}});
    CHECK(parse_register("zf") == RegisterName{Flag::ZF});
    CHECK_FALSE(parse_register("eax").has_value());
}

TEST_CASE("indirizzi seg:off e off", "[parse]") {
    Registers regs;
    regs.set(SegReg::CS, 0x1000);
    regs.set(SegReg::DS, 0x2000);
    regs.set(Reg16::BX, 0x0123);
    regs.ip = 0x0456;

    CHECK(parse_address("3000:0100", regs, SegReg::CS) == Address{0x3000, 0x0100});
    CHECK(parse_address("100", regs, SegReg::CS) == Address{0x1000, 0x0100});
    CHECK(parse_address("100", regs, SegReg::DS) == Address{0x2000, 0x0100});
    CHECK(parse_address("ds:bx", regs, SegReg::CS) == Address{0x2000, 0x0123});
    CHECK(parse_address("cs:ip", regs, SegReg::DS) == Address{0x1000, 0x0456});
    // "cf" è un flag, ma come offset vale come numero esadecimale
    CHECK(parse_address("ds:cf", regs, SegReg::CS) == Address{0x2000, 0x00CF});

    CHECK_FALSE(parse_address("xyz:100", regs, SegReg::CS).has_value());
    CHECK_FALSE(parse_address("ds:al", regs, SegReg::CS).has_value());
    CHECK_FALSE(parse_address("ds:10000", regs, SegReg::CS).has_value());
}
