#include "core/loader.hpp"

namespace emu {

bool load_program(Memory& memory, Cpu& cpu, std::span<const std::uint8_t> program,
                  const LoadConfig& config) {
    if (program.size() > max_program_size(config)) {
        return false;
    }

    memory.clear();
    memory.load(physical_address(config.segment, config.ip), program);

    cpu.reset();
    Registers& regs = cpu.regs();
    for (const SegReg s : {SegReg::ES, SegReg::CS, SegReg::SS, SegReg::DS}) {
        regs.set(s, config.segment);
    }
    regs.ip = config.ip;
    regs.set(Reg16::SP, config.sp);
    return true;
}

}  // namespace emu
