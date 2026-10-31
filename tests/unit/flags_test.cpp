#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/flags.hpp"

using namespace emu;

TEST_CASE("ogni flag occupa il bit previsto dall'8086", "[flags]") {
    struct Case {
        Flag flag;
        std::uint16_t bit;
    };
    const Case cases[] = {
        {Flag::CF, 0x0001}, {Flag::PF, 0x0004}, {Flag::AF, 0x0010},
        {Flag::ZF, 0x0040}, {Flag::SF, 0x0080}, {Flag::TF, 0x0100},
        {Flag::IF, 0x0200}, {Flag::DF, 0x0400}, {Flag::OF, 0x0800},
    };
    for (const Case& c : cases) {
        INFO("bit " << c.bit);
        Flags f;
        f.set(c.flag, true);
        CHECK(f.test(c.flag));
        CHECK(f.word() == (Flags::kForcedOnes | c.bit));
    }
}

TEST_CASE("i bit riservati si leggono sempre al valore fisso", "[flags]") {
    Flags f;
    CHECK(f.word() == 0xF002);
    f.set_word(0xFFFF);
    CHECK(f.word() == 0xFFD7);  // bit 3 e 5 a 0
    f.set_word(0x0000);
    CHECK(f.word() == 0xF002);  // bit 1 e 12..15 a 1
}

TEST_CASE("set modifica un solo flag", "[flags]") {
    Flags f;
    f.set_word(0xFFFF);
    f.set(Flag::ZF, false);
    CHECK_FALSE(f.test(Flag::ZF));
    CHECK(f.word() == 0xFF97);
    f.set(Flag::ZF, true);
    CHECK(f.word() == 0xFFD7);
}
