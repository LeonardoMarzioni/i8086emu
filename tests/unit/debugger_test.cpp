#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "debugger/debugger.hpp"

using namespace emu;
using namespace emu::dbg;
using Catch::Matchers::ContainsSubstring;

namespace {

// Somma 5 + 4 + 3 + 2 + 1 con una subroutine, poi HLT.
const std::vector<std::uint8_t> kSumProgram{
    0xB9, 0x05, 0x00,  // 0100: mov cx,0x5
    0x31, 0xC0,        // 0103: xor ax,ax
    0xE8, 0x06, 0x00,  // 0105: call 0x10e
    0x49,              // 0108: dec cx
    0x75, 0xFA,        // 0109: jnz 0x105
    0xF4,              // 010B: hlt
    0x90, 0x90,        // 010C: nop, nop
    0x01, 0xC8,        // 010E: add ax,cx
    0xC3,              // 0110: ret
};

std::string run(Debugger& dbg, std::string_view command) {
    std::ostringstream out;
    dbg.execute(command, out);
    return out.str();
}

void load(Debugger& dbg, const std::vector<std::uint8_t>& program) {
    std::ostringstream out;
    REQUIRE(dbg.load_bytes(program, "test", out));
}

const Registers& regs(Debugger& dbg) {
    return dbg.cpu().regs();
}

}  // namespace

TEST_CASE("load mostra dove è stato caricato il programma", "[debugger]") {
    Debugger dbg;
    std::ostringstream out;
    REQUIRE(dbg.load_bytes(kSumProgram, "sum.bin", out));
    CHECK_THAT(out.str(), ContainsSubstring("loaded 17 bytes from sum.bin at 1000:0100"));
    CHECK_THAT(out.str(), ContainsSubstring("1000:0100  B90500         mov cx,0x5"));
}

TEST_CASE("step esegue n istruzioni e mostra la successiva", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    CHECK_THAT(run(dbg, "step"), ContainsSubstring("1000:0103  31C0           xor ax,ax"));
    CHECK(regs(dbg).get(Reg16::CX) == 5);
    run(dbg, "s 2");
    CHECK(regs(dbg).ip == 0x010E);  // dentro la subroutine
    CHECK_THAT(run(dbg, "step 0"), ContainsSubstring("usage: step [n]"));
}

TEST_CASE("next scavalca una CALL", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    run(dbg, "step 2");
    REQUIRE(regs(dbg).ip == 0x0105);  // call 0x10e
    CHECK_THAT(run(dbg, "next"), ContainsSubstring("1000:0108"));
    CHECK(regs(dbg).ip == 0x0108);
    CHECK(regs(dbg).get(Reg16::AX) == 5);  // la subroutine è stata eseguita
    CHECK(regs(dbg).get(Reg16::SP) == 0xFFFE);
}

TEST_CASE("next su un'istruzione che non è CALL fa come step", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    run(dbg, "n");
    CHECK(regs(dbg).ip == 0x0103);
}

TEST_CASE("next in una ricorsione si ferma al ritorno della stessa chiamata", "[debugger]") {
    Debugger dbg;
    load(dbg, {
        0xB9, 0x03, 0x00,  // 0100: mov cx,0x3
        0xE8, 0x01, 0x00,  // 0103: call 0x107
        0xF4,              // 0106: hlt
        0x49,              // 0107: f: dec cx
        0x74, 0x03,        // 0108: jz 0x10d
        0xE8, 0xFA, 0xFF,  // 010A: call 0x107   (ritorna a 010D)
        0x40,              // 010D: inc ax
        0xC3,              // 010E: ret
    });
    run(dbg, "step 4");
    REQUIRE(regs(dbg).ip == 0x010A);  // la CALL ricorsiva del primo livello
    run(dbg, "next");
    // Le chiamate più interne passano da 010D con SP più basso: next le ignora.
    CHECK(regs(dbg).ip == 0x010D);
    CHECK(regs(dbg).get(Reg16::SP) == 0xFFFC);
    CHECK(regs(dbg).get(Reg16::AX) == 2);
}

TEST_CASE("continue si ferma ai breakpoint", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    CHECK_THAT(run(dbg, "b 10e"), ContainsSubstring("breakpoint 1 at 1000:010E"));
    CHECK_THAT(run(dbg, "c"), ContainsSubstring("breakpoint 1 at 1000:010E"));
    CHECK(regs(dbg).ip == 0x010E);
    CHECK(regs(dbg).get(Reg16::AX) == 0);

    // Ripartendo da un breakpoint non ci si ferma subito: si arriva al giro successivo.
    run(dbg, "continue");
    CHECK(regs(dbg).ip == 0x010E);
    CHECK(regs(dbg).get(Reg16::AX) == 5);
    CHECK(regs(dbg).get(Reg16::CX) == 4);
}

TEST_CASE("elenco e cancellazione dei breakpoint", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    CHECK_THAT(run(dbg, "break"), ContainsSubstring("no breakpoints"));
    run(dbg, "b 108");
    run(dbg, "b 1000:10e");
    CHECK_THAT(run(dbg, "b cs:108"), ContainsSubstring("breakpoint 1 is already at 1000:0108"));
    const std::string list = run(dbg, "b");
    CHECK_THAT(list, ContainsSubstring("1  1000:0108"));
    CHECK_THAT(list, ContainsSubstring("2  1000:010E"));
    CHECK_THAT(run(dbg, "delete 1"), ContainsSubstring("breakpoint 1 deleted"));
    CHECK_THAT(run(dbg, "delete 1"), ContainsSubstring("no breakpoint 1"));
    run(dbg, "delete");
    CHECK_THAT(run(dbg, "break"), ContainsSubstring("no breakpoints"));
}

TEST_CASE("continue fino a HLT, poi serve reset", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    CHECK_THAT(run(dbg, "run"), ContainsSubstring("halted at 1000:010B"));
    CHECK(regs(dbg).get(Reg16::AX) == 15);
    CHECK_THAT(run(dbg, "step"), ContainsSubstring("cpu halted at 1000:010C, use reset to restart"));

    run(dbg, "set cs:100 90");  // modifica il codice caricato
    CHECK_THAT(run(dbg, "reset"), ContainsSubstring("reset, reloaded test"));
    CHECK_FALSE(dbg.cpu().halted());
    CHECK(regs(dbg).ip == 0x0100);
    CHECK(regs(dbg).get(Reg16::AX) == 0);
    CHECK(dbg.memory().read8(0x1000, 0x0100) == 0xB9);  // immagine originale
}

TEST_CASE("un opcode non implementato ferma l'esecuzione", "[debugger]") {
    Debugger dbg;
    load(dbg, {0x90, 0x10, 0xC0});  // nop; adc al,al
    CHECK_THAT(run(dbg, "c"), ContainsSubstring("unimplemented opcode 10 at 1000:0101"));
    CHECK(regs(dbg).ip == 0x0101);

    load(dbg, {0xF6, 0xE0});  // mul al: gruppo 3, reg = 4
    CHECK_THAT(run(dbg, "step"), ContainsSubstring("unimplemented opcode F6 /4 at 1000:0100"));
}

TEST_CASE("il limite di istruzioni ferma un ciclo infinito", "[debugger]") {
    DebuggerConfig config;
    config.instruction_limit = 100;
    Debugger dbg(config);
    load(dbg, {0xEB, 0xFE});  // jmp short $
    CHECK_THAT(run(dbg, "c"), ContainsSubstring("instruction limit (100) reached at 1000:0100"));
}

TEST_CASE("Ctrl-C interrompe continue", "[debugger]") {
    DebuggerConfig config;
    config.instruction_limit = UINT64_MAX;
    Debugger dbg(config);
    load(dbg, {0xEB, 0xFE});  // jmp short $

    std::ostringstream out;
    std::atomic<bool> finished{false};
    std::thread runner([&] {
        dbg.execute("continue", out);
        finished = true;
    });
    // Il flag viene azzerato all'inizio di continue: lo si reimposta finché non si ferma,
    // come farebbe una persona che preme Ctrl-C più volte.
    while (!finished) {
        dbg.interrupt_flag() = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    runner.join();
    CHECK_THAT(out.str(), ContainsSubstring("interrupted at 1000:0100"));
}

TEST_CASE("regs, flags e x", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    const std::string r = run(dbg, "regs");
    CHECK_THAT(r, ContainsSubstring("AX=0000  BX=0000  CX=0000"));
    CHECK_THAT(r, ContainsSubstring("CS=1000  IP=0100  FLAGS=F002 []"));
    CHECK_THAT(run(dbg, "flags"), ContainsSubstring("CF=0  PF=0  AF=0  ZF=0"));
    CHECK_THAT(run(dbg, "x cs:100 4"), ContainsSubstring("1000:0100  B9 05 00 31"));
}

TEST_CASE("set cambia registri, flag e memoria", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    CHECK_THAT(run(dbg, "set ax 1234"), ContainsSubstring("AX=1234"));
    CHECK(regs(dbg).get(Reg16::AX) == 0x1234);
    run(dbg, "set al ff");
    CHECK(regs(dbg).get(Reg16::AX) == 0x12FF);
    run(dbg, "set ZF 1");
    CHECK(regs(dbg).flags.test(Flag::ZF));
    CHECK_THAT(run(dbg, "set flags 0"), ContainsSubstring("FLAGS=F002"));
    run(dbg, "set ds 2000");
    CHECK_THAT(run(dbg, "set 200 41 42"), ContainsSubstring("wrote 2 bytes at 2000:0200"));
    CHECK(dbg.memory().read8(0x2000, 0x0201) == 0x42);

    CHECK_THAT(run(dbg, "set bh 100"), ContainsSubstring("value too large"));
    CHECK_THAT(run(dbg, "set zf 2"), ContainsSubstring("0 or 1"));
    CHECK_THAT(run(dbg, "set bx 1 2"), ContainsSubstring("usage: set"));
}

TEST_CASE("disasm elenca le istruzioni da CS:IP", "[debugger]") {
    Debugger dbg;
    load(dbg, kSumProgram);
    run(dbg, "b 105");
    const std::string listing = run(dbg, "disasm 3");
    CHECK_THAT(listing, ContainsSubstring(" > 1000:0100  B90500         mov cx,0x5"));
    CHECK_THAT(listing, ContainsSubstring("   1000:0103  31C0           xor ax,ax"));
    CHECK_THAT(listing, ContainsSubstring("*  1000:0105  E80600         call 0x10e"));
}

TEST_CASE("load da file, file mancante e programma troppo grande", "[debugger]") {
    const auto path = std::filesystem::temp_directory_path() / "i8086emu_debugger_test.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(kSumProgram.data()),
                   static_cast<std::streamsize>(kSumProgram.size()));
    }
    Debugger dbg;
    CHECK_THAT(run(dbg, "load " + path.string()), ContainsSubstring("loaded 17 bytes"));
    std::filesystem::remove(path);

    CHECK_THAT(run(dbg, "load " + path.string()), ContainsSubstring("cannot open"));

    std::ostringstream out;
    CHECK_FALSE(dbg.load_bytes(std::vector<std::uint8_t>(0xFF01, 0x90), "big", out));
    CHECK_THAT(out.str(), ContainsSubstring("program too large: 65281 bytes (max 65280)"));
}

TEST_CASE("help, comandi sconosciuti e quit", "[debugger]") {
    Debugger dbg;
    CHECK_THAT(run(dbg, "help"), ContainsSubstring("step [n] (s)"));
    CHECK_THAT(run(dbg, "frobnicate"), ContainsSubstring("unknown command 'frobnicate'"));
    std::ostringstream out;
    CHECK(dbg.execute("", out));
    CHECK_FALSE(dbg.execute("quit", out));
}
