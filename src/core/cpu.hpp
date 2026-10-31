#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "core/alu.hpp"
#include "core/decoder.hpp"
#include "core/memory.hpp"
#include "core/opcode_table.hpp"
#include "core/registers.hpp"

namespace emu {

struct StepResult {
    enum class Status : std::uint8_t {
        Executed,       // istruzione eseguita
        Halted,         // la CPU è ferma su HLT (eseguita ora o in precedenza)
        Unimplemented,  // opcode non implementato: lo stato non è stato modificato
    };

    Status status = Status::Executed;
    std::uint16_t cs = 0;  // indirizzo dell'istruzione
    std::uint16_t ip = 0;
    std::uint8_t opcode = 0;
    std::optional<std::uint8_t> group_reg;  // per gli opcode di gruppo, ad esempio "F6 /4"
};

class Cpu {
public:
    // La CPU non possiede la memoria: sono componenti distinti collegati dal bus,
    // e debugger e harness accedono alla memoria anche senza passare dalla CPU.
    explicit Cpu(Memory& memory) noexcept;

    // Una CPU è legata alla sua memoria: copiarla creerebbe una seconda CPU sullo stesso bus.
    Cpu(const Cpu&) = delete;
    Cpu& operator=(const Cpu&) = delete;

    // Registri a zero (FLAGS vale quindi F002h), CPU non ferma. Non è il reset hardware
    // dell'8086 (CS:IP = FFFF:0000, dove c'è il BIOS): lo stato iniziale di un programma
    // lo imposta il caricatore.
    void reset() noexcept;

    // Esegue un'istruzione, prefissi compresi.
    StepResult step();

    Registers& regs() noexcept { return regs_; }
    const Registers& regs() const noexcept { return regs_; }
    Memory& memory() noexcept { return memory_; }
    const Memory& memory() const noexcept { return memory_; }
    bool halted() const noexcept { return halted_; }

private:
    using Handler = void (Cpu::*)(const Instruction&);
    using HandlerTable = std::array<Handler, kMnemonicCount>;

    // Esecuzione guidata da tabella: un gestore per mnemonico, nullptr se non implementato.
    static constexpr HandlerTable make_handlers() noexcept;
    static const HandlerTable kHandlers;

    // Indirizzo effettivo: base + indice + spiazzamento, con wrap a 16 bit.
    std::uint16_t effective_address(const MemoryRef& m) const noexcept;

    // Accesso generico agli operandi: registri, registri di segmento, immediati e memoria.
    // La larghezza serve per gli operandi in memoria (un byte o una parola).
    std::uint16_t read_operand(const Operand& op, Width w) const noexcept;
    void write_operand(const Operand& op, Width w, std::uint16_t value) noexcept;

    // Stack: sempre SS:SP, senza segment override (Tabella 2-2). Cresce verso il basso.
    void decrement_sp() noexcept;
    void push_word(std::uint16_t value) noexcept;
    std::uint16_t pop_word() noexcept;

    void exec_mov(const Instruction& ins);
    template <AluOp Op>
    void exec_alu(const Instruction& ins);
    void exec_inc(const Instruction& ins);
    void exec_dec(const Instruction& ins);
    void exec_neg(const Instruction& ins);
    void exec_not(const Instruction& ins);
    void exec_nop(const Instruction& ins);
    void exec_hlt(const Instruction& ins);
    void exec_push(const Instruction& ins);
    void exec_pop(const Instruction& ins);
    void exec_jmp(const Instruction& ins);
    void exec_jz(const Instruction& ins);
    void exec_jnz(const Instruction& ins);
    void exec_call(const Instruction& ins);
    void exec_ret(const Instruction& ins);

    Memory& memory_;
    Registers regs_;
    bool halted_ = false;
};

}  // namespace emu
