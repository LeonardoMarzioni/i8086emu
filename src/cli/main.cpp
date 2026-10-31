// Front-end a riga di comando: legge i comandi da stdin e li passa al debugger.
// Qui stanno solo le parti legate al terminale: argomenti, prompt, Ctrl-C.

#include <atomic>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif

#include "debugger/debugger.hpp"
#include "debugger/parse.hpp"

namespace {

// Un gestore di segnale può solo scrivere su un atomico lock-free (o su volatile sig_atomic_t).
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool>* g_interrupt = nullptr;

void on_sigint(int) {
    if (g_interrupt != nullptr) {
        g_interrupt->store(true);
    }
}

bool stdin_is_terminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

void print_usage(std::ostream& out) {
    out << "usage: i8086emu [--segment HEX] [--ip HEX] [--sp HEX] [--limit N] [program]\n"
           "  --segment  value of CS, DS, ES, SS at load time (default 1000)\n"
           "  --ip       load offset and initial IP (default 0100)\n"
           "  --sp       initial SP (default FFFE)\n"
           "  --limit    max instructions for continue/next (default 1000000)\n";
}

}  // namespace

int main(int argc, char** argv) {
    emu::dbg::DebuggerConfig config;
    std::string program;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto next_value = [&]() -> std::optional<std::string_view> {
            if (i + 1 < argc) {
                return std::string_view(argv[++i]);
            }
            return std::nullopt;
        };
        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        }
        if (arg == "--segment" || arg == "--ip" || arg == "--sp") {
            const auto value = next_value();
            const auto hex = value ? emu::dbg::parse_hex16(*value) : std::nullopt;
            if (!hex) {
                std::cerr << "invalid value for " << arg << '\n';
                return 2;
            }
            std::uint16_t& target = arg == "--segment" ? config.load.segment
                                    : arg == "--ip"    ? config.load.ip
                                                       : config.load.sp;
            target = *hex;
        } else if (arg == "--limit") {
            const auto value = next_value();
            const auto count = value ? emu::dbg::parse_count(*value) : std::nullopt;
            if (!count || *count == 0) {
                std::cerr << "invalid value for --limit\n";
                return 2;
            }
            config.instruction_limit = *count;
        } else if (arg.starts_with('-') || !program.empty()) {
            print_usage(std::cerr);
            return 2;
        } else {
            program = arg;
        }
    }

    emu::dbg::Debugger debugger(config);
    g_interrupt = &debugger.interrupt_flag();
    std::signal(SIGINT, on_sigint);

    if (!program.empty() && !debugger.load_file(program, std::cout)) {
        return 1;
    }

    // Il prompt serve solo a una persona: con i comandi da un file o da una pipe l'output
    // resta pulito e confrontabile.
    const bool interactive = stdin_is_terminal();
    std::string line;
    while (true) {
        if (interactive) {
            std::cout << "i8086> " << std::flush;
        }
        if (!std::getline(std::cin, line)) {
            break;  // fine dell'input (Ctrl-D)
        }
        if (!debugger.execute(line, std::cout)) {
            break;
        }
    }
    return 0;
}
