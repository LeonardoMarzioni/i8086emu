#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/cpu.hpp"
#include "core/loader.hpp"
#include "core/memory.hpp"
#include "debugger/parse.hpp"

namespace emu::dbg {

struct DebuggerConfig {
    LoadConfig load;                              // stato iniziale dei registri al caricamento
    std::uint64_t instruction_limit = 1'000'000;  // continue e next: protezione dai cicli infiniti
};

// Logica del debugger, separata dal front-end: riceve una riga di comando alla volta e
// scrive il risultato su uno stream, quindi si può provare senza un terminale.
class Debugger {
public:
    explicit Debugger(DebuggerConfig config = {});

    Debugger(const Debugger&) = delete;
    Debugger& operator=(const Debugger&) = delete;

    // Esegue una riga di comando. Restituisce false quando il comando chiede di uscire.
    bool execute(std::string_view line, std::ostream& out);

    // Caricano un programma come il comando load. Restituiscono false in caso di errore.
    bool load_file(const std::string& path, std::ostream& out);
    bool load_bytes(std::span<const std::uint8_t> program, std::string_view name, std::ostream& out);

    // Flag di interruzione (Ctrl-C). Il front-end lo imposta dal gestore di SIGINT, che può
    // solo scrivere un atomico lock-free; l'esecuzione lo controlla prima di ogni istruzione.
    std::atomic<bool>& interrupt_flag() noexcept { return interrupt_; }

    Cpu& cpu() noexcept { return cpu_; }
    Memory& memory() noexcept { return memory_; }

private:
    using Args = std::vector<std::string_view>;
    using Handler = bool (Debugger::*)(const Args&, std::ostream&);

    // Tabella dei comandi: nome, alias, sintassi e descrizione. Da qui si genera anche l'help.
    struct Command {
        std::string_view name;
        std::array<std::string_view, 2> aliases;
        std::string_view usage;
        std::string_view description;
        Handler handler;
    };
    static const std::array<Command, 14> kCommands;

    struct Breakpoint {
        int id = 0;
        Address address;
    };

    // Dove si ferma next dopo una CALL: l'istruzione successiva, con lo stesso SP di prima
    // della CALL. Controllare anche SP evita di fermarsi prima del tempo in una ricorsione,
    // quando una chiamata più interna passa dallo stesso indirizzo.
    struct ReturnPoint {
        std::uint32_t address = 0;
        std::uint16_t sp = 0;
    };

    enum class StopKind { Done, Breakpoint, Halted, Unimplemented, Interrupted, Limit };

    struct Stop {
        StopKind kind = StopKind::Done;
        StepResult result;   // per Halted e Unimplemented
        int breakpoint = 0;  // per Breakpoint
    };

    // Esegue al massimo max_steps istruzioni. Con use_breakpoints si ferma prima di eseguire
    // un'istruzione con un breakpoint, tranne la prima: si riparte da lì.
    Stop run(std::uint64_t max_steps, bool use_breakpoints, std::optional<ReturnPoint> until);

    void report(const Stop& stop, std::ostream& out) const;
    void print_current(std::ostream& out) const;
    void print_usage(std::string_view command, std::ostream& out) const;
    bool refuse_if_halted(std::ostream& out) const;
    std::optional<int> breakpoint_at(std::uint32_t physical) const;
    std::uint32_t current_address() const;

    bool cmd_step(const Args& args, std::ostream& out);
    bool cmd_next(const Args& args, std::ostream& out);
    bool cmd_continue(const Args& args, std::ostream& out);
    bool cmd_break(const Args& args, std::ostream& out);
    bool cmd_delete(const Args& args, std::ostream& out);
    bool cmd_regs(const Args& args, std::ostream& out);
    bool cmd_flags(const Args& args, std::ostream& out);
    bool cmd_examine(const Args& args, std::ostream& out);
    bool cmd_set(const Args& args, std::ostream& out);
    bool cmd_disasm(const Args& args, std::ostream& out);
    bool cmd_load(const Args& args, std::ostream& out);
    bool cmd_reset(const Args& args, std::ostream& out);
    bool cmd_help(const Args& args, std::ostream& out);
    bool cmd_quit(const Args& args, std::ostream& out);

    DebuggerConfig config_;
    Memory memory_;
    Cpu cpu_{memory_};
    std::vector<std::uint8_t> program_;  // immagine caricata, per reset
    std::string program_name_;
    std::vector<Breakpoint> breakpoints_;
    int next_breakpoint_id_ = 1;
    std::atomic<bool> interrupt_{false};
};

}  // namespace emu::dbg
