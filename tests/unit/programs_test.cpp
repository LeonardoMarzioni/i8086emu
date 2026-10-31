#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/cpu.hpp"
#include "core/loader.hpp"

using namespace emu;

// I programmi di prova (programs/*.asm, assemblati con NASM) eseguiti fino a HLT, con il
// controllo dei risultati che lasciano in memoria a partire da 1000:0200.

namespace {

constexpr std::uint16_t kSegment = 0x1000;  // segmento predefinito del caricatore

struct Machine {
    Memory memory;
    Cpu cpu{memory};
};

std::vector<std::uint8_t> read_program(const std::string& name) {
    std::ifstream file(std::filesystem::path(I8086EMU_PROGRAMS_DIR) / name, std::ios::binary);
    REQUIRE(file);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
}

// Carica ed esegue fino a HLT. Un opcode non implementato o un ciclo infinito fanno fallire
// il test: i programmi devono usare solo il sottoinsieme e terminare con HLT.
void run_program(Machine& m, const std::string& name) {
    REQUIRE(load_program(m.memory, m.cpu, read_program(name)));
    for (int i = 0; i < 100'000; ++i) {
        const StepResult r = m.cpu.step();
        if (r.status == StepResult::Status::Halted) {
            return;
        }
        REQUIRE(r.status == StepResult::Status::Executed);
    }
    FAIL("instruction limit reached");
}

std::string read_string(const Memory& memory, std::uint16_t offset) {
    std::string text;
    for (std::uint8_t c = memory.read8(kSegment, offset); c != 0; c = memory.read8(kSegment, ++offset)) {
        text += static_cast<char>(c);
    }
    return text;
}

}  // namespace

TEST_CASE("fibonacci.bin calcola F(0)..F(23)", "[programs]") {
    Machine m;
    run_program(m, "fibonacci.bin");
    std::uint16_t a = 0;
    std::uint16_t b = 1;
    for (std::uint16_t i = 0; i < 24; ++i) {
        INFO("F(" << i << ")");
        CHECK(m.memory.read16(kSegment, static_cast<std::uint16_t>(0x200 + 2 * i)) == a);
        const auto next = static_cast<std::uint16_t>(a + b);
        a = b;
        b = next;
    }
    CHECK(m.cpu.regs().get(Reg16::AX) == 28657);  // F(23)
    CHECK(m.memory.read16(kSegment, 0x200 + 2 * 24) == 0x0000);  // nulla scritto oltre l'array
}

TEST_CASE("sort.bin ordina l'array e lascia lo stack in ordine", "[programs]") {
    Machine m;
    run_program(m, "sort.bin");
    const std::uint16_t expected[] = {61, 87, 170, 275, 426, 503, 512, 653, 897, 908};
    for (std::uint16_t i = 0; i < 10; ++i) {
        INFO("elemento " << i);
        CHECK(m.memory.read16(kSegment, static_cast<std::uint16_t>(0x200 + 2 * i)) == expected[i]);
    }
    CHECK(m.cpu.regs().get(Reg16::SP) == 0xFFFE);  // ogni PUSH e CALL ha il suo POP e RET
}

TEST_CASE("strings.bin: lunghezza, maiuscolo e rovesciata", "[programs]") {
    Machine m;
    run_program(m, "strings.bin");
    CHECK(read_string(m.memory, 0x200) == "Hello, 8086 world!");  // non modificata
    CHECK(m.memory.read16(kSegment, 0x220) == 18);
    CHECK(read_string(m.memory, 0x230) == "HELLO, 8086 WORLD!");
    CHECK(read_string(m.memory, 0x250) == "!dlrow 6808 ,olleH");
    // Nessuna scrittura oltre il terminatore delle copie
    CHECK(m.memory.read8(kSegment, 0x230 + 19) == 0xFF);
    CHECK(m.memory.read8(kSegment, 0x250 + 19) == 0xFF);
}
