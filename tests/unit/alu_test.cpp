#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/alu.hpp"

using namespace emu;

// I risultati attesi sono ricavati a mano dalle definizioni dei flag. Le stesse formule
// sono state confrontate con SingleStepTests (opcode 04-3D accumulatore, 40, 48,
// F6/F7 /3, FE /0 /1): la verifica sistematica è nell'harness di SingleStepTests.

namespace {

constexpr std::uint16_t C = 0x001;
constexpr std::uint16_t P = 0x004;
constexpr std::uint16_t A = 0x010;
constexpr std::uint16_t Z = 0x040;
constexpr std::uint16_t S = 0x080;
constexpr std::uint16_t O = 0x800;
constexpr std::uint16_t kArith = C | P | A | Z | S | O;

std::uint16_t arith_flags(const Flags& f) {
    return static_cast<std::uint16_t>(f.word() & kArith);
}

struct BinaryCase {
    const char* name;
    AluOp op;
    Width width;
    std::uint16_t a;
    std::uint16_t b;
    std::uint16_t result;
    std::uint16_t flags;
};

const BinaryCase kBinaryCases[] = {
    {"ADD 01+01", AluOp::Add, Width::Byte, 0x01, 0x01, 0x02, 0},
    {"ADD FF+01", AluOp::Add, Width::Byte, 0xFF, 0x01, 0x00, C | A | Z | P},
    {"ADD 7F+01", AluOp::Add, Width::Byte, 0x7F, 0x01, 0x80, A | S | O},
    {"ADD 80+80", AluOp::Add, Width::Byte, 0x80, 0x80, 0x00, C | Z | P | O},
    {"ADD 08+08", AluOp::Add, Width::Byte, 0x08, 0x08, 0x10, A},
    {"ADD FFFF+0001", AluOp::Add, Width::Word, 0xFFFF, 0x0001, 0x0000, C | A | Z | P},
    {"ADD 7FFF+0001 (PF guarda solo il byte basso)", AluOp::Add, Width::Word, 0x7FFF, 0x0001,
     0x8000, A | S | P | O},
    {"ADD 1234+4321", AluOp::Add, Width::Word, 0x1234, 0x4321, 0x5555, P},
    {"SUB 00-01", AluOp::Sub, Width::Byte, 0x00, 0x01, 0xFF, C | A | S | P},
    {"SUB 80-01", AluOp::Sub, Width::Byte, 0x80, 0x01, 0x7F, A | O},
    {"SUB 05-05", AluOp::Sub, Width::Byte, 0x05, 0x05, 0x00, Z | P},
    {"SUB 10-01", AluOp::Sub, Width::Byte, 0x10, 0x01, 0x0F, A | P},
    {"SUB 0000-0001", AluOp::Sub, Width::Word, 0x0000, 0x0001, 0xFFFF, C | A | S | P},
    {"SUB 8000-0001", AluOp::Sub, Width::Word, 0x8000, 0x0001, 0x7FFF, A | P | O},
    {"CMP 05-07", AluOp::Cmp, Width::Byte, 0x05, 0x07, 0xFE, C | A | S},
    {"AND F0&0F", AluOp::And, Width::Byte, 0xF0, 0x0F, 0x00, Z | P},
    {"OR 8000|0001", AluOp::Or, Width::Word, 0x8000, 0x0001, 0x8001, S},
    {"XOR FF^0F", AluOp::Xor, Width::Byte, 0xFF, 0x0F, 0xF0, S | P},
    {"AND ignora i bit oltre la larghezza", AluOp::And, Width::Byte, 0x12FF, 0x340F, 0x0F, P},
};

enum class Unary { Inc, Dec, Neg };

struct UnaryCase {
    const char* name;
    Unary kind;
    Width width;
    std::uint16_t a;
    bool carry_before;
    std::uint16_t result;
    std::uint16_t flags;
};

const UnaryCase kUnaryCases[] = {
    {"INC FF con CF=1", Unary::Inc, Width::Byte, 0xFF, true, 0x00, C | A | Z | P},
    {"INC 7F", Unary::Inc, Width::Byte, 0x7F, false, 0x80, A | S | O},
    {"INC FFFF con CF=0", Unary::Inc, Width::Word, 0xFFFF, false, 0x0000, A | Z | P},
    {"DEC 00 con CF=0", Unary::Dec, Width::Byte, 0x00, false, 0xFF, A | S | P},
    {"DEC 80", Unary::Dec, Width::Byte, 0x80, false, 0x7F, A | O},
    {"DEC 0001 con CF=1", Unary::Dec, Width::Word, 0x0001, true, 0x0000, C | Z | P},
    {"NEG 00 azzera CF", Unary::Neg, Width::Byte, 0x00, true, 0x00, Z | P},
    {"NEG 01", Unary::Neg, Width::Byte, 0x01, false, 0xFF, C | A | S | P},
    {"NEG 80", Unary::Neg, Width::Byte, 0x80, false, 0x80, C | S | O},
    {"NEG 8000", Unary::Neg, Width::Word, 0x8000, false, 0x8000, C | S | P | O},
    {"NEG 10", Unary::Neg, Width::Byte, 0x10, false, 0xF0, C | S | P},
};

std::uint16_t apply(Unary kind, Width w, std::uint16_t a, Flags& f) {
    switch (kind) {
    case Unary::Inc: return alu_inc(w, a, f);
    case Unary::Dec: return alu_dec(w, a, f);
    case Unary::Neg: return alu_neg(w, a, f);
    }
    return 0;
}

}  // namespace

TEST_CASE("operazioni a due operandi: risultato e flag", "[alu]") {
    for (const BinaryCase& c : kBinaryCases) {
        INFO(c.name);
        Flags f;
        f.set_word(0xFFFF);  // tutti i flag a 1: ognuno deve essere riscritto
        const std::uint16_t result = alu_binary(c.op, c.width, c.a, c.b, f);
        CHECK(result == c.result);
        CHECK(arith_flags(f) == c.flags);
        // TF, IF e DF non vengono toccati dall'ALU
        CHECK(f.test(Flag::TF));
        CHECK(f.test(Flag::IF));
        CHECK(f.test(Flag::DF));
    }
}

TEST_CASE("INC, DEC e NEG: risultato e flag", "[alu]") {
    for (const UnaryCase& c : kUnaryCases) {
        INFO(c.name);
        Flags f;
        f.set(Flag::CF, c.carry_before);
        const std::uint16_t result = apply(c.kind, c.width, c.a, f);
        CHECK(result == c.result);
        CHECK(arith_flags(f) == c.flags);
    }
}

TEST_CASE("NOT inverte i bit nella larghezza data", "[alu]") {
    CHECK(alu_not(Width::Byte, 0x0F) == 0xF0);
    CHECK(alu_not(Width::Byte, 0xAB00) == 0xFF);  // conta solo il byte basso
    CHECK(alu_not(Width::Word, 0x1234) == 0xEDCB);
}
