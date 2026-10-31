// Harness di SingleStepTests (set 8088, versione 2): per ogni test carica lo stato iniziale,
// esegue un'istruzione con l'emulatore e confronta lo stato finale con quello atteso.
//
// Uso:
//   ssts_runner --list METADATA                elenca i file di test del sottoinsieme
//   ssts_runner [--markdown] [--examples N] DATA_DIR [FILE...]
//
// I dati si scaricano e si decomprimono con scripts/fetch_ssts.sh. Nei test lo stato "final"
// contiene solo i registri e le celle di memoria cambiati (FLAGS compare intero se è
// cambiato almeno un flag); i campi "queue" e "cycles" si ignorano.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/cpu.hpp"
#include "core/decoder.hpp"
#include "core/memory.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace emu;

namespace {

// --- Registri nei file di test ---

enum class Slot : std::uint8_t { Gpr, Seg, Ip, Flags };

struct RegisterField {
    std::string_view name;
    Slot slot;
    std::uint8_t index;  // codifica ModR/M per Gpr e Seg
};

constexpr std::array<RegisterField, 14> kRegisterFields = {{
    {"ax", Slot::Gpr, 0}, {"bx", Slot::Gpr, 3}, {"cx", Slot::Gpr, 1}, {"dx", Slot::Gpr, 2},
    {"sp", Slot::Gpr, 4}, {"bp", Slot::Gpr, 5}, {"si", Slot::Gpr, 6}, {"di", Slot::Gpr, 7},
    {"cs", Slot::Seg, 1}, {"ds", Slot::Seg, 3}, {"es", Slot::Seg, 0}, {"ss", Slot::Seg, 2},
    {"ip", Slot::Ip, 0},  {"flags", Slot::Flags, 0},
}};

std::uint16_t read_field(const Registers& r, const RegisterField& f) {
    switch (f.slot) {
    case Slot::Gpr: return r.get(static_cast<Reg16>(f.index));
    case Slot::Seg: return r.get(static_cast<SegReg>(f.index));
    case Slot::Ip: return r.ip;
    case Slot::Flags: return r.flags.word();
    }
    return 0;
}

void write_field(Registers& r, const RegisterField& f, std::uint16_t value) {
    switch (f.slot) {
    case Slot::Gpr: r.set(static_cast<Reg16>(f.index), value); break;
    case Slot::Seg: r.set(static_cast<SegReg>(f.index), value); break;
    case Slot::Ip: r.ip = value; break;
    case Slot::Flags: r.flags.set_word(value); break;  // i bit riservati vengono normalizzati
    }
}

// --- Scelta dei file del sottoinsieme ---

struct TestFile {
    std::string name;    // "8B", oppure "80.4" per un opcode di gruppo con reg = 4
    std::string status;  // stato in metadata.json: normal, alias, undocumented, ...
};

// Il sottoinsieme si ricava dal decoder stesso: un opcode, o un opcode di gruppo con un dato
// campo reg, ne fa parte se il decoder non lo dichiara non implementato. Così harness ed
// emulatore non possono divergere su cosa è implementato.
class SubsetProbe {
public:
    bool implemented(std::uint8_t opcode, std::uint8_t reg) {
        memory_.write8(0, 0, opcode);
        memory_.write8(0, 1, static_cast<std::uint8_t>(0xC0 | (reg << 3)));  // ModR/M con mod = 11
        return decode(memory_, 0, 0).mnemonic != Mnemonic::Unimplemented;
    }

private:
    Memory memory_;
};

std::vector<TestFile> subset_files(const json& metadata) {
    SubsetProbe probe;
    std::vector<TestFile> files;
    const json& opcodes = metadata.at("opcodes");
    for (unsigned op = 0; op < 256; ++op) {
        const std::string key = std::format("{:02X}", op);
        if (!opcodes.contains(key)) {
            continue;
        }
        const json& entry = opcodes.at(key);
        const auto opcode = static_cast<std::uint8_t>(op);
        if (entry.contains("reg")) {
            // Opcode di gruppo: un file per ogni valore del campo reg
            for (std::uint8_t reg = 0; reg < 8; ++reg) {
                const std::string reg_key = std::to_string(reg);
                if (entry.at("reg").contains(reg_key) && probe.implemented(opcode, reg)) {
                    files.push_back({key + "." + reg_key, entry.at("reg").at(reg_key).value("status", "?")});
                }
            }
            continue;
        }
        // I prefissi non hanno file di test propri: compaiono davanti alle altre istruzioni.
        const std::string status = entry.value("status", "?");
        if (status == "prefix") {
            continue;
        }
        for (std::uint8_t reg = 0; reg < 8; ++reg) {
            if (probe.implemented(opcode, reg)) {
                files.push_back({key, status});
                break;
            }
        }
    }
    return files;
}

// Nomi dei file come sono sul disco. Per alcuni opcode di gruppo (C6, C7) la suite ha un solo
// file per l'intero opcode anche se metadata.json li divide per campo reg: in quel caso
// "C6.0" diventa "C6", e i test con un altro reg risultano non implementati.
std::vector<TestFile> resolve_files(const std::vector<TestFile>& subset, const fs::path& data_dir) {
    std::vector<TestFile> files;
    for (const TestFile& f : subset) {
        const std::size_t dot = f.name.find('.');
        const std::string whole = f.name.substr(0, dot);
        if (dot != std::string::npos && !fs::exists(data_dir / (f.name + ".json")) &&
            fs::exists(data_dir / (whole + ".json"))) {
            if (files.empty() || files.back().name != whole) {
                files.push_back({whole, f.status});
            }
            continue;
        }
        files.push_back(f);  // se non esiste, il report lo segnala come mancante
    }
    return files;
}

// Indice del byte di opcode, dopo gli eventuali prefissi.
std::size_t opcode_index(const json& bytes) {
    std::size_t i = 0;
    while (i < bytes.size() && kOpcodeTable[bytes[i].get<std::uint8_t>()].mnemonic == Mnemonic::SegmentPrefix) {
        ++i;
    }
    return i;
}

// Maschera dei flag definiti per l'istruzione di un test: per gli opcode di gruppo dipende
// dal campo reg del ModR/M (ad esempio AF è indefinito per OR, AND, XOR ma non per ADD).
std::uint16_t flags_mask_for(const json& opcodes, const json& bytes) {
    const std::size_t i = opcode_index(bytes);
    const std::string key = std::format("{:02X}", bytes.at(i).get<unsigned>());
    if (!opcodes.contains(key)) {
        return 0xFFFF;
    }
    const json& entry = opcodes.at(key);
    if (entry.contains("reg") && i + 1 < bytes.size()) {
        const std::string reg = std::to_string((bytes.at(i + 1).get<unsigned>() >> 3) & 7);
        return entry.at("reg").contains(reg)
                   ? entry.at("reg").at(reg).value("flags-mask", std::uint16_t{0xFFFF})
                   : std::uint16_t{0xFFFF};
    }
    return entry.value("flags-mask", std::uint16_t{0xFFFF});
}

// --- Esecuzione dei test ---

struct FileResult {
    TestFile file;
    bool missing = false;
    std::size_t total = 0;
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;                          // forme che l'emulatore non implementa
    std::map<std::string, std::size_t> skipped_forms;  // "8C /4" -> numero di test
    std::vector<std::string> examples;                // primi test falliti, descritti
};

std::string bytes_text(const json& bytes) {
    std::string text;
    for (const json& b : bytes) {
        text += std::format("{}{:02X}", text.empty() ? "" : " ", b.get<unsigned>());
    }
    return text;
}

// Opcode e campo reg di un test, saltando i prefissi: serve a raggruppare i test saltati.
std::string form_of(const json& bytes) {
    const std::size_t i = opcode_index(bytes);
    if (i + 1 >= bytes.size()) {
        return "?";
    }
    return std::format("{:02X} /{}", bytes[i].get<unsigned>(), (bytes[i + 1].get<unsigned>() >> 3) & 7);
}

// Confronta lo stato dell'emulatore con quello atteso: registri e celle iniziali, con sopra
// quelli che "final" indica come cambiati. Restituisce le differenze trovate.
std::vector<std::string> compare(const json& test, const Registers& regs, const Memory& memory,
                                 std::uint16_t flags_mask) {
    std::vector<std::string> diffs;
    const json& initial_regs = test.at("initial").at("regs");
    const json& final_regs = test.at("final").at("regs");
    for (const RegisterField& f : kRegisterFields) {
        const std::string name(f.name);
        const json& source = final_regs.contains(name) ? final_regs : initial_regs;
        const auto expected = source.at(name).get<std::uint16_t>();
        const std::uint16_t actual = read_field(regs, f);
        if (f.slot == Slot::Flags) {
            // Solo i flag definiti: bit riservati esclusi, flag indefiniti azzerati dalla maschera
            const auto mask = static_cast<std::uint16_t>(flags_mask & Flags::kDefined);
            if (((actual ^ expected) & mask) != 0) {
                diffs.push_back(std::format("flags {:04X}, expected {:04X} (mask {:04X})", actual,
                                            expected, mask));
            }
        } else if (actual != expected) {
            diffs.push_back(std::format("{} {:04X}, expected {:04X}", name, actual, expected));
        }
    }

    std::map<std::uint32_t, std::uint8_t> expected_ram;
    for (const json* ram : {&test.at("initial").at("ram"), &test.at("final").at("ram")}) {
        for (const json& cell : *ram) {
            expected_ram[cell.at(0).get<std::uint32_t>()] = cell.at(1).get<std::uint8_t>();
        }
    }
    for (const auto& [address, expected] : expected_ram) {
        const std::uint8_t actual = memory.peek(address);
        if (actual != expected) {
            diffs.push_back(std::format("[{:05X}] {:02X}, expected {:02X}", address, actual, expected));
        }
    }
    return diffs;
}

FileResult run_file(const fs::path& data_dir, const TestFile& file, const json& opcodes,
                    Memory& memory, Cpu& cpu, std::size_t max_examples) {
    FileResult result;
    result.file = file;
    std::ifstream in(data_dir / (file.name + ".json"));
    if (!in) {
        result.missing = true;
        return result;
    }
    const json tests = json::parse(in);

    for (const json& test : tests) {
        ++result.total;
        const json& initial = test.at("initial");

        cpu.reset();
        Registers& regs = cpu.regs();
        for (const RegisterField& f : kRegisterFields) {
            write_field(regs, f, initial.at("regs").at(std::string(f.name)).get<std::uint16_t>());
        }
        for (const json& cell : initial.at("ram")) {
            memory.poke(cell.at(0).get<std::uint32_t>(), cell.at(1).get<std::uint8_t>());
        }

        const StepResult step = cpu.step();
        if (step.status == StepResult::Status::Unimplemented) {
            ++result.skipped;
            ++result.skipped_forms[form_of(test.at("bytes"))];
        } else {
            const std::uint16_t mask = flags_mask_for(opcodes, test.at("bytes"));
            const std::vector<std::string> diffs = compare(test, regs, memory, mask);
            if (diffs.empty()) {
                ++result.passed;
            } else {
                ++result.failed;
                if (result.examples.size() < max_examples) {
                    std::string text = std::format("{} (idx {}, bytes {}):", test.value("name", "?"),
                                                   test.value("idx", -1), bytes_text(test.at("bytes")));
                    for (const std::string& d : diffs) {
                        text += " " + d + ";";
                    }
                    result.examples.push_back(text);
                }
            }
        }

        // Riporta a zero le celle del test: il test successivo trova la memoria vuota.
        for (const json* ram : {&initial.at("ram"), &test.at("final").at("ram")}) {
            for (const json& cell : *ram) {
                memory.poke(cell.at(0).get<std::uint32_t>(), 0);
            }
        }
    }
    return result;
}

// --- Report ---

std::string skipped_text(const FileResult& r) {
    std::string text;
    for (const auto& [form, count] : r.skipped_forms) {
        text += std::format("{}{} ({})", text.empty() ? "" : ", ", form, count);
    }
    return text;
}

void print_text_row(const FileResult& r) {
    if (r.missing) {
        std::cout << std::format("{:<6} {:<13} missing\n", r.file.name, r.file.status);
        return;
    }
    std::cout << std::format("{:<6} {:<13} {:>6} {:>7} {:>7} {:>7}", r.file.name, r.file.status,
                             r.total, r.passed, r.failed, r.skipped);
    if (r.skipped > 0) {
        std::cout << "  not implemented: " << skipped_text(r);
    }
    std::cout << '\n';
    for (const std::string& example : r.examples) {
        std::cout << "         FAIL " << example << '\n';
    }
}

void print_markdown(const std::vector<FileResult>& results, const std::string& source) {
    std::size_t total = 0, passed = 0, failed = 0, skipped = 0, missing = 0;
    for (const FileResult& r : results) {
        total += r.total;
        passed += r.passed;
        failed += r.failed;
        skipped += r.skipped;
        missing += r.missing ? 1 : 0;
    }
    std::cout << "# Risultati di SingleStepTests\n\n"
                 "File generato da `ssts_runner --markdown` (vedi README).\n\n";
    std::cout << std::format("- Suite: SingleStepTests 8088 v2, commit `{}`\n", source);
    std::cout << std::format("- File del sottoinsieme: {}, di cui senza test nella suite: {}\n",
                             results.size(), missing);
    std::cout << std::format("- Test: {}, superati {}, falliti {}, saltati {}\n\n", total, passed,
                             failed, skipped);
    std::cout << "I test *saltati* riguardano forme che l'emulatore non implementa per scelta "
                 "(vedi README, disallineamenti tra documentazione e hardware).\n\n";
    std::cout << "| File | Stato | Test | Superati | Falliti | Saltati | Forme non implementate |\n"
                 "|---|---|---:|---:|---:|---:|---|\n";
    for (const FileResult& r : results) {
        if (r.missing) {
            std::cout << std::format("| {} | {} | – | – | – | – | file non presente nella suite |\n",
                                     r.file.name, r.file.status);
            continue;
        }
        std::cout << std::format("| {} | {} | {} | {} | {} | {} | {} |\n", r.file.name, r.file.status,
                                 r.total, r.passed, r.failed, r.skipped, skipped_text(r));
    }
}

void print_usage() {
    std::cerr << "usage: ssts_runner --list METADATA\n"
                 "       ssts_runner [--markdown] [--examples N] DATA_DIR [FILE...]\n";
}

std::optional<json> read_json(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open " << path << '\n';
        return std::nullopt;
    }
    return json::parse(in);
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "--list") {
        const auto metadata = read_json(args[1]);
        if (!metadata) {
            return 2;
        }
        for (const TestFile& f : subset_files(*metadata)) {
            std::cout << f.name << '\n';
        }
        return 0;
    }

    bool markdown = false;
    std::size_t max_examples = 3;
    std::optional<fs::path> data_dir;
    std::vector<std::string> only;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--markdown") {
            markdown = true;
        } else if (args[i] == "--examples" && i + 1 < args.size()) {
            max_examples = std::stoul(std::string(args[++i]));
        } else if (args[i].starts_with("--")) {
            print_usage();
            return 2;
        } else if (!data_dir) {
            data_dir = fs::path(args[i]);
        } else {
            only.emplace_back(args[i]);
        }
    }
    if (!data_dir) {
        print_usage();
        return 2;
    }

    const auto metadata = read_json(*data_dir / "metadata.json");
    if (!metadata) {
        return 2;
    }
    std::vector<TestFile> files = resolve_files(subset_files(*metadata), *data_dir);
    if (!only.empty()) {
        std::erase_if(files, [&](const TestFile& f) {
            return std::find(only.begin(), only.end(), f.name) == only.end();
        });
    }

    std::string source = "?";
    if (std::ifstream in(*data_dir / "SOURCE"); in) {
        std::getline(in, source);
    }

    Memory memory;
    Cpu cpu(memory);
    const auto start = std::chrono::steady_clock::now();
    if (!markdown) {
        std::cout << std::format("{:<6} {:<13} {:>6} {:>7} {:>7} {:>7}\n", "file", "status", "tests",
                                 "passed", "failed", "skipped");
    }
    std::vector<FileResult> results;
    for (const TestFile& file : files) {
        results.push_back(run_file(*data_dir, file, metadata->at("opcodes"), memory, cpu, max_examples));
        if (!markdown) {
            print_text_row(results.back());
        }
    }

    std::size_t total = 0, passed = 0, failed = 0, skipped = 0;
    for (const FileResult& r : results) {
        total += r.total;
        passed += r.passed;
        failed += r.failed;
        skipped += r.skipped;
    }
    if (markdown) {
        print_markdown(results, source);
    } else {
        std::cout << std::format("total: {} files, {} tests, {} passed, {} failed, {} skipped\n",
                                 results.size(), total, passed, failed, skipped);
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cerr << std::format("elapsed: {:.1f} s\n", seconds);
    return failed == 0 ? 0 : 1;
}
