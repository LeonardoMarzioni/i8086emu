#include "debugger/debugger.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <ostream>
#include <variant>

#include "core/disassembler.hpp"

namespace emu::dbg {
namespace {

// Gli indirizzi si mostrano sempre come segmento:offset.
std::string address_text(std::uint16_t segment, std::uint16_t offset) {
    return std::format("{:04X}:{:04X}", segment, offset);
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// "unimplemented opcode XX at CS:IP"; per gli opcode di gruppo anche il campo reg ("F6 /4").
std::string unimplemented_text(const StepResult& r) {
    const std::string group = r.group_reg ? std::format(" /{}", *r.group_reg) : "";
    return std::format("unimplemented opcode {:02X}{} at {}", r.opcode, group,
                       address_text(r.cs, r.ip));
}

struct FlagName {
    Flag flag;
    std::string_view name;
};

// Dal bit più alto al più basso, come nel registro.
constexpr std::array<FlagName, 9> kFlagNames = {{
    {Flag::OF, "OF"}, {Flag::DF, "DF"}, {Flag::IF, "IF"}, {Flag::TF, "TF"}, {Flag::SF, "SF"},
    {Flag::ZF, "ZF"}, {Flag::AF, "AF"}, {Flag::PF, "PF"}, {Flag::CF, "CF"},
}};

}  // namespace

const std::array<Debugger::Command, 14> Debugger::kCommands = {{
    {"step", {"s", ""}, "step [n]", "execute n instructions (default 1)", &Debugger::cmd_step},
    {"next", {"n", ""}, "next", "like step, but a CALL runs until it returns", &Debugger::cmd_next},
    {"continue", {"c", "run"}, "continue",
     "run until a breakpoint, HLT, an unimplemented opcode, Ctrl-C or the instruction limit",
     &Debugger::cmd_continue},
    {"break", {"b", ""}, "break [address]",
     "set a breakpoint (default segment CS); without address, list the breakpoints",
     &Debugger::cmd_break},
    {"delete", {"", ""}, "delete [n]", "delete breakpoint n, or all the breakpoints",
     &Debugger::cmd_delete},
    {"regs", {"", ""}, "regs", "show the registers", &Debugger::cmd_regs},
    {"flags", {"", ""}, "flags", "show the flags", &Debugger::cmd_flags},
    {"x", {"", ""}, "x address [n]", "dump n bytes of memory (default 16, default segment DS)",
     &Debugger::cmd_examine},
    {"set", {"", ""}, "set reg value | set address byte...",
     "change a register, a flag (0/1) or memory bytes (default segment DS)", &Debugger::cmd_set},
    {"disasm", {"", ""}, "disasm [n]", "disassemble n instructions from CS:IP (default 10)",
     &Debugger::cmd_disasm},
    {"load", {"", ""}, "load file", "load a flat binary program, .COM style", &Debugger::cmd_load},
    {"reset", {"", ""}, "reset", "reload the program and reset the registers", &Debugger::cmd_reset},
    {"help", {"", ""}, "help", "show this help", &Debugger::cmd_help},
    {"quit", {"", ""}, "quit", "exit the debugger", &Debugger::cmd_quit},
}};

Debugger::Debugger(DebuggerConfig config) : config_(config) {
    // Stato iniziale senza programma: memoria vuota, registri come dopo un caricamento.
    static_cast<void>(load_program(memory_, cpu_, {}, config_.load));
}

bool Debugger::execute(std::string_view line, std::ostream& out) {
    const Args words = split_words(line);
    if (words.empty()) {
        return true;
    }
    const std::string name = lower(words.front());
    for (const Command& command : kCommands) {
        if (command.name == name || command.aliases[0] == name || command.aliases[1] == name) {
            const Args args(std::next(words.begin()), words.end());
            return (this->*command.handler)(args, out);
        }
    }
    out << "unknown command '" << words.front() << "' (type help)\n";
    return true;
}

bool Debugger::load_file(const std::string& path, std::ostream& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        out << "cannot open '" << path << "'\n";
        return false;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    return load_bytes(bytes, path, out);
}

bool Debugger::load_bytes(std::span<const std::uint8_t> program, std::string_view name,
                          std::ostream& out) {
    if (!load_program(memory_, cpu_, program, config_.load)) {
        out << std::format("program too large: {} bytes (max {})\n", program.size(),
                           max_program_size(config_.load));
        return false;
    }
    program_.assign(program.begin(), program.end());
    program_name_ = name;
    out << std::format("loaded {} bytes from {} at {}\n", program.size(), name,
                       address_text(config_.load.segment, config_.load.ip));
    print_current(out);
    return true;
}

// --- Esecuzione ---

std::uint32_t Debugger::current_address() const {
    return physical_address(cpu_.regs().get(SegReg::CS), cpu_.regs().ip);
}

// I breakpoint si confrontano per indirizzo fisico: lo stesso byte raggiunto con una coppia
// segmento:offset diversa ferma comunque l'esecuzione.
std::optional<int> Debugger::breakpoint_at(std::uint32_t physical) const {
    for (const Breakpoint& b : breakpoints_) {
        if (physical_address(b.address.segment, b.address.offset) == physical) {
            return b.id;
        }
    }
    return std::nullopt;
}

Debugger::Stop Debugger::run(std::uint64_t max_steps, bool use_breakpoints,
                             std::optional<ReturnPoint> until) {
    interrupt_.store(false);  // un Ctrl-C premuto al prompt non riguarda questa esecuzione
    for (std::uint64_t executed = 0;; ++executed) {
        if (interrupt_.exchange(false)) {
            return {StopKind::Interrupted, {}, 0};
        }
        if (executed > 0 && use_breakpoints) {
            if (until && current_address() == until->address &&
                cpu_.regs().get(Reg16::SP) == until->sp) {
                return {StopKind::Done, {}, 0};
            }
            if (const auto id = breakpoint_at(current_address())) {
                return {StopKind::Breakpoint, {}, *id};
            }
        }
        if (executed == max_steps) {
            return {use_breakpoints ? StopKind::Limit : StopKind::Done, {}, 0};
        }
        const StepResult result = cpu_.step();
        if (result.status == StepResult::Status::Halted) {
            return {StopKind::Halted, result, 0};
        }
        if (result.status == StepResult::Status::Unimplemented) {
            return {StopKind::Unimplemented, result, 0};
        }
    }
}

void Debugger::report(const Stop& stop, std::ostream& out) const {
    const std::string here = address_text(cpu_.regs().get(SegReg::CS), cpu_.regs().ip);
    switch (stop.kind) {
    case StopKind::Done:
        break;
    case StopKind::Breakpoint:
        out << std::format("breakpoint {} at {}\n", stop.breakpoint, here);
        break;
    case StopKind::Halted:
        out << "halted at " << address_text(stop.result.cs, stop.result.ip) << '\n';
        return;  // dopo HLT non c'è un'istruzione successiva da mostrare
    case StopKind::Unimplemented:
        out << unimplemented_text(stop.result) << '\n';
        break;
    case StopKind::Interrupted:
        out << "interrupted at " << here << '\n';
        break;
    case StopKind::Limit:
        out << std::format("instruction limit ({}) reached at {}\n", config_.instruction_limit, here);
        break;
    }
    print_current(out);
}

// Riga di disassemblato con un marcatore di due caratteri: '*' se c'è un breakpoint,
// '>' se è l'istruzione corrente.
void Debugger::print_current(std::ostream& out) const {
    const Registers& r = cpu_.regs();
    const DisassembledLine line = disassemble(memory_, r.get(SegReg::CS), r.ip);
    std::string bytes;
    for (const std::uint8_t b : line.bytes) {
        bytes += std::format("{:02X}", b);
    }
    const char mark = breakpoint_at(current_address()) ? '*' : ' ';
    out << std::format("{}> {}  {:<14} {}\n", mark, address_text(r.get(SegReg::CS), r.ip), bytes,
                       line.text);
}

void Debugger::print_usage(std::string_view command, std::ostream& out) const {
    for (const Command& c : kCommands) {
        if (c.name == command) {
            out << "usage: " << c.usage << '\n';
            return;
        }
    }
}

bool Debugger::refuse_if_halted(std::ostream& out) const {
    if (!cpu_.halted()) {
        return false;
    }
    out << "cpu halted at " << address_text(cpu_.regs().get(SegReg::CS), cpu_.regs().ip)
        << ", use reset to restart\n";
    return true;
}

// --- Comandi ---

bool Debugger::cmd_step(const Args& args, std::ostream& out) {
    std::uint64_t count = 1;
    if (!args.empty()) {
        const auto n = parse_count(args[0]);
        if (args.size() > 1 || !n || *n == 0) {
            print_usage("step", out);
            return true;
        }
        count = *n;
    }
    if (!refuse_if_halted(out)) {
        report(run(count, false, std::nullopt), out);
    }
    return true;
}

bool Debugger::cmd_next(const Args& args, std::ostream& out) {
    if (!args.empty()) {
        print_usage("next", out);
        return true;
    }
    if (refuse_if_halted(out)) {
        return true;
    }
    const Registers& r = cpu_.regs();
    const Instruction ins = decode(memory_, r.get(SegReg::CS), r.ip);
    if (ins.mnemonic != Mnemonic::Call) {
        report(run(1, false, std::nullopt), out);
        return true;
    }
    const auto return_ip = static_cast<std::uint16_t>(r.ip + ins.length);
    const ReturnPoint until{physical_address(r.get(SegReg::CS), return_ip), r.get(Reg16::SP)};
    report(run(config_.instruction_limit, true, until), out);
    return true;
}

bool Debugger::cmd_continue(const Args& args, std::ostream& out) {
    if (!args.empty()) {
        print_usage("continue", out);
        return true;
    }
    if (!refuse_if_halted(out)) {
        report(run(config_.instruction_limit, true, std::nullopt), out);
    }
    return true;
}

bool Debugger::cmd_break(const Args& args, std::ostream& out) {
    if (args.empty()) {
        if (breakpoints_.empty()) {
            out << "no breakpoints\n";
        }
        for (const Breakpoint& b : breakpoints_) {
            out << std::format("{:>3}  {}\n", b.id, address_text(b.address.segment, b.address.offset));
        }
        return true;
    }
    const auto address = args.size() == 1 ? parse_address(args[0], cpu_.regs(), SegReg::CS) : std::nullopt;
    if (!address) {
        print_usage("break", out);
        return true;
    }
    const std::string text = address_text(address->segment, address->offset);
    if (const auto existing = breakpoint_at(physical_address(address->segment, address->offset))) {
        out << std::format("breakpoint {} is already at {}\n", *existing, text);
        return true;
    }
    breakpoints_.push_back({next_breakpoint_id_, *address});
    out << std::format("breakpoint {} at {}\n", next_breakpoint_id_++, text);
    return true;
}

bool Debugger::cmd_delete(const Args& args, std::ostream& out) {
    if (args.empty()) {
        breakpoints_.clear();
        out << "all breakpoints deleted\n";
        return true;
    }
    const auto id = args.size() == 1 ? parse_count(args[0]) : std::nullopt;
    if (!id) {
        print_usage("delete", out);
        return true;
    }
    const auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(), [&](const Breakpoint& b) {
        return static_cast<std::uint64_t>(b.id) == *id;
    });
    if (it == breakpoints_.end()) {
        out << "no breakpoint " << *id << '\n';
        return true;
    }
    breakpoints_.erase(it);
    out << "breakpoint " << *id << " deleted\n";
    return true;
}

bool Debugger::cmd_regs(const Args& args, std::ostream& out) {
    if (!args.empty()) {
        print_usage("regs", out);
        return true;
    }
    const Registers& r = cpu_.regs();
    out << std::format("AX={:04X}  BX={:04X}  CX={:04X}  DX={:04X}  SP={:04X}  BP={:04X}  SI={:04X}  DI={:04X}\n",
                       r.get(Reg16::AX), r.get(Reg16::BX), r.get(Reg16::CX), r.get(Reg16::DX),
                       r.get(Reg16::SP), r.get(Reg16::BP), r.get(Reg16::SI), r.get(Reg16::DI));
    std::string set_flags;
    for (const FlagName& f : kFlagNames) {
        if (r.flags.test(f.flag)) {
            set_flags += set_flags.empty() ? "" : " ";
            set_flags += f.name;
        }
    }
    out << std::format("DS={:04X}  ES={:04X}  SS={:04X}  CS={:04X}  IP={:04X}  FLAGS={:04X} [{}]\n",
                       r.get(SegReg::DS), r.get(SegReg::ES), r.get(SegReg::SS), r.get(SegReg::CS),
                       r.ip, r.flags.word(), set_flags);
    print_current(out);
    return true;
}

bool Debugger::cmd_flags(const Args& args, std::ostream& out) {
    if (!args.empty()) {
        print_usage("flags", out);
        return true;
    }
    const Flags& flags = cpu_.regs().flags;
    for (auto it = kFlagNames.rbegin(); it != kFlagNames.rend(); ++it) {
        out << std::format("{}={}  ", it->name, flags.test(it->flag) ? 1 : 0);
    }
    out << std::format("(FLAGS={:04X})\n", flags.word());
    return true;
}

bool Debugger::cmd_examine(const Args& args, std::ostream& out) {
    if (args.empty() || args.size() > 2) {
        print_usage("x", out);
        return true;
    }
    const auto address = parse_address(args[0], cpu_.regs(), SegReg::DS);
    const auto count = args.size() == 2 ? parse_count(args[1]) : std::optional<std::uint64_t>{16};
    if (!address || !count || *count == 0 || *count > 0x10000) {
        print_usage("x", out);
        return true;
    }
    // Righe da 16 byte; l'offset fa wrap dentro il segmento come per la CPU.
    for (std::uint64_t done = 0; done < *count; done += 16) {
        const auto offset = static_cast<std::uint16_t>(address->offset + done);
        const std::uint64_t n = std::min<std::uint64_t>(16, *count - done);
        std::string hex;
        std::string ascii;
        for (std::uint64_t i = 0; i < 16; ++i) {
            if (i < n) {
                const std::uint8_t b = memory_.read8(address->segment, static_cast<std::uint16_t>(offset + i));
                hex += std::format(" {:02X}", b);
                ascii += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
            } else {
                hex += "   ";
            }
        }
        out << std::format("{} {}  |{}|\n", address_text(address->segment, offset), hex, ascii);
    }
    return true;
}

bool Debugger::cmd_set(const Args& args, std::ostream& out) {
    if (args.size() < 2) {
        print_usage("set", out);
        return true;
    }
    Registers& r = cpu_.regs();

    // I nomi di registro e di flag hanno la precedenza sugli indirizzi: per scrivere
    // all'offset CF si usa "0xcf" o "ds:cf", per la memoria puntata da BX "ds:bx".
    if (const auto reg = parse_register(args[0])) {
        const auto value = args.size() == 2 ? parse_hex16(args[1]) : std::nullopt;
        if (!value) {
            print_usage("set", out);
            return true;
        }
        const std::string name = upper(args[0]);
        if (const auto* flag = std::get_if<Flag>(&*reg)) {
            if (*value > 1) {
                out << "a flag can only be 0 or 1\n";
                return true;
            }
            r.flags.set(*flag, *value == 1);
            out << std::format("{}={}\n", name, *value);
        } else if (const auto* r8 = std::get_if<Reg8>(&*reg)) {
            if (*value > 0xFF) {
                out << "value too large for an 8-bit register\n";
                return true;
            }
            r.set(*r8, static_cast<std::uint8_t>(*value));
            out << std::format("{}={:02X}\n", name, *value);
        } else {
            if (const auto* r16 = std::get_if<Reg16>(&*reg)) {
                r.set(*r16, *value);
            } else if (const auto* seg = std::get_if<SegReg>(&*reg)) {
                r.set(*seg, *value);
            } else if (std::holds_alternative<InstructionPointer>(*reg)) {
                r.ip = *value;
            } else {
                r.flags.set_word(*value);
            }
            out << std::format("{}={:04X}\n", name, std::holds_alternative<FlagsRegister>(*reg)
                                                        ? r.flags.word() : *value);
        }
        return true;
    }

    const auto address = parse_address(args[0], r, SegReg::DS);
    if (!address) {
        print_usage("set", out);
        return true;
    }
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto value = parse_hex16(args[i]);
        if (!value || *value > 0xFF) {
            out << "bytes must be between 00 and FF\n";
            return true;
        }
        bytes.push_back(static_cast<std::uint8_t>(*value));
    }
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        memory_.write8(address->segment, static_cast<std::uint16_t>(address->offset + i), bytes[i]);
    }
    out << std::format("wrote {} bytes at {}\n", bytes.size(),
                       address_text(address->segment, address->offset));
    return true;
}

bool Debugger::cmd_disasm(const Args& args, std::ostream& out) {
    std::uint64_t count = 10;
    if (!args.empty()) {
        const auto n = parse_count(args[0]);
        if (args.size() > 1 || !n || *n == 0) {
            print_usage("disasm", out);
            return true;
        }
        count = *n;
    }
    const std::uint16_t cs = cpu_.regs().get(SegReg::CS);
    std::uint16_t ip = cpu_.regs().ip;
    for (std::uint64_t i = 0; i < count; ++i) {
        const DisassembledLine line = disassemble(memory_, cs, ip);
        std::string bytes;
        for (const std::uint8_t b : line.bytes) {
            bytes += std::format("{:02X}", b);
        }
        const char mark = breakpoint_at(physical_address(cs, ip)) ? '*' : ' ';
        const char current = i == 0 ? '>' : ' ';
        out << std::format("{}{} {}  {:<14} {}\n", mark, current, address_text(cs, ip), bytes, line.text);
        ip = static_cast<std::uint16_t>(ip + line.bytes.size());
    }
    return true;
}

bool Debugger::cmd_load(const Args& args, std::ostream& out) {
    if (args.size() != 1) {
        print_usage("load", out);
        return true;
    }
    load_file(std::string(args[0]), out);
    return true;
}

bool Debugger::cmd_reset(const Args& args, std::ostream& out) {
    if (!args.empty()) {
        print_usage("reset", out);
        return true;
    }
    // L'immagine è già stata controllata al caricamento: qui non può essere troppo grande.
    static_cast<void>(load_program(memory_, cpu_, program_, config_.load));
    if (program_name_.empty()) {
        out << "reset (no program loaded)\n";
    } else {
        out << "reset, reloaded " << program_name_ << '\n';
    }
    print_current(out);
    return true;
}

bool Debugger::cmd_help(const Args&, std::ostream& out) {
    out << "commands:\n";
    for (const Command& c : kCommands) {
        std::string aliases;
        for (const std::string_view alias : c.aliases) {
            if (!alias.empty()) {
                aliases += aliases.empty() ? "" : ", ";
                aliases += alias;
            }
        }
        const std::string name = aliases.empty() ? std::string(c.usage)
                                                 : std::format("{} ({})", c.usage, aliases);
        out << std::format("  {:<38} {}\n", name, c.description);
    }
    out << "addresses: seg:off or off; seg is hex or es/cs/ss/ds, off is hex, a 16-bit register or ip\n"
           "numbers: addresses and values are hexadecimal (0x optional), counts are decimal\n";
    return true;
}

bool Debugger::cmd_quit(const Args&, std::ostream&) {
    return false;
}

}  // namespace emu::dbg
