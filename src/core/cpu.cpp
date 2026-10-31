#include "core/cpu.hpp"

#include <cassert>
#include <cstddef>

namespace emu {
namespace {

constexpr std::size_t mnemonic_index(Mnemonic m) noexcept {
    return static_cast<std::size_t>(m);
}

}  // namespace

constexpr Cpu::HandlerTable Cpu::make_handlers() noexcept {
    HandlerTable t{};  // tutti nullptr: non implementati
    t[mnemonic_index(Mnemonic::Mov)] = &Cpu::exec_mov;
    t[mnemonic_index(Mnemonic::Add)] = &Cpu::exec_alu<AluOp::Add>;
    t[mnemonic_index(Mnemonic::Or)] = &Cpu::exec_alu<AluOp::Or>;
    t[mnemonic_index(Mnemonic::And)] = &Cpu::exec_alu<AluOp::And>;
    t[mnemonic_index(Mnemonic::Sub)] = &Cpu::exec_alu<AluOp::Sub>;
    t[mnemonic_index(Mnemonic::Xor)] = &Cpu::exec_alu<AluOp::Xor>;
    t[mnemonic_index(Mnemonic::Cmp)] = &Cpu::exec_alu<AluOp::Cmp>;
    t[mnemonic_index(Mnemonic::Inc)] = &Cpu::exec_inc;
    t[mnemonic_index(Mnemonic::Dec)] = &Cpu::exec_dec;
    t[mnemonic_index(Mnemonic::Neg)] = &Cpu::exec_neg;
    t[mnemonic_index(Mnemonic::Not)] = &Cpu::exec_not;
    t[mnemonic_index(Mnemonic::Nop)] = &Cpu::exec_nop;
    t[mnemonic_index(Mnemonic::Hlt)] = &Cpu::exec_hlt;
    t[mnemonic_index(Mnemonic::Push)] = &Cpu::exec_push;
    t[mnemonic_index(Mnemonic::Pop)] = &Cpu::exec_pop;
    t[mnemonic_index(Mnemonic::Jmp)] = &Cpu::exec_jmp;
    t[mnemonic_index(Mnemonic::Jz)] = &Cpu::exec_jz;
    t[mnemonic_index(Mnemonic::Jnz)] = &Cpu::exec_jnz;
    t[mnemonic_index(Mnemonic::Call)] = &Cpu::exec_call;
    t[mnemonic_index(Mnemonic::Ret)] = &Cpu::exec_ret;
    return t;
}

constinit const Cpu::HandlerTable Cpu::kHandlers = Cpu::make_handlers();

Cpu::Cpu(Memory& memory) noexcept : memory_(memory) {}

void Cpu::reset() noexcept {
    regs_ = Registers{};
    halted_ = false;
}

StepResult Cpu::step() {
    StepResult result;
    result.cs = regs_.get(SegReg::CS);
    result.ip = regs_.ip;
    if (halted_) {
        result.status = StepResult::Status::Halted;
        return result;
    }

    const Instruction ins = decode(memory_, result.cs, result.ip);
    result.opcode = ins.opcode;
    result.group_reg = ins.group_reg;

    const Handler handler = kHandlers[mnemonic_index(ins.mnemonic)];
    if (handler == nullptr) {
        // La decodifica non ha effetti collaterali: registri e memoria sono intatti
        // e IP punta ancora all'inizio dell'istruzione (al primo prefisso).
        result.status = StepResult::Status::Unimplemented;
        return result;
    }

    // Come sull'8086, durante l'esecuzione IP punta già all'istruzione successiva:
    // è l'indirizzo di ritorno che CALL salva sullo stack.
    regs_.ip = static_cast<std::uint16_t>(regs_.ip + ins.length);
    (this->*handler)(ins);

    result.status = halted_ ? StepResult::Status::Halted : StepResult::Status::Executed;
    return result;
}

std::uint16_t Cpu::effective_address(const MemoryRef& m) const noexcept {
    std::uint32_t sum = m.displacement;
    if (m.base) {
        sum += regs_.get(*m.base);
    }
    if (m.index) {
        sum += regs_.get(*m.index);
    }
    return static_cast<std::uint16_t>(sum);  // wrap dentro il segmento
}

std::uint16_t Cpu::read_operand(const Operand& op, Width w) const noexcept {
    switch (op.type) {
    case Operand::Type::Reg8:
        return regs_.get(static_cast<Reg8>(op.reg));
    case Operand::Type::Reg16:
        return regs_.get(static_cast<Reg16>(op.reg));
    case Operand::Type::Seg:
        return regs_.get(static_cast<SegReg>(op.reg));
    case Operand::Type::Imm:
    case Operand::Type::Target:
        return op.imm;
    case Operand::Type::Mem: {
        const std::uint16_t segment = regs_.get(op.mem.segment);
        const std::uint16_t offset = effective_address(op.mem);
        return w == Width::Byte ? memory_.read8(segment, offset) : memory_.read16(segment, offset);
    }
    case Operand::Type::None:
        break;
    }
    assert(false && "lettura di un operando assente");
    return 0;
}

void Cpu::write_operand(const Operand& op, Width w, std::uint16_t value) noexcept {
    switch (op.type) {
    case Operand::Type::Reg8:
        regs_.set(static_cast<Reg8>(op.reg), static_cast<std::uint8_t>(value));
        return;
    case Operand::Type::Reg16:
        regs_.set(static_cast<Reg16>(op.reg), value);
        return;
    case Operand::Type::Seg:
        regs_.set(static_cast<SegReg>(op.reg), value);
        return;
    case Operand::Type::Mem: {
        const std::uint16_t segment = regs_.get(op.mem.segment);
        const std::uint16_t offset = effective_address(op.mem);
        if (w == Width::Byte) {
            memory_.write8(segment, offset, static_cast<std::uint8_t>(value));
        } else {
            memory_.write16(segment, offset, value);
        }
        return;
    }
    case Operand::Type::Imm:
    case Operand::Type::Target:
    case Operand::Type::None:
        break;
    }
    assert(false && "scrittura su un operando non scrivibile");
}

void Cpu::decrement_sp() noexcept {
    regs_.set(Reg16::SP, static_cast<std::uint16_t>(regs_.get(Reg16::SP) - 2));
}

void Cpu::push_word(std::uint16_t value) noexcept {
    decrement_sp();
    memory_.write16(regs_.get(SegReg::SS), regs_.get(Reg16::SP), value);
}

std::uint16_t Cpu::pop_word() noexcept {
    const std::uint16_t value = memory_.read16(regs_.get(SegReg::SS), regs_.get(Reg16::SP));
    regs_.set(Reg16::SP, static_cast<std::uint16_t>(regs_.get(Reg16::SP) + 2));
    return value;
}

void Cpu::exec_mov(const Instruction& ins) {
    write_operand(ins.dst, ins.width, read_operand(ins.src, ins.width));
}

template <AluOp Op>
void Cpu::exec_alu(const Instruction& ins) {
    const std::uint16_t result = alu_binary(Op, ins.width, read_operand(ins.dst, ins.width),
                                            read_operand(ins.src, ins.width), regs_.flags);
    if constexpr (Op != AluOp::Cmp) {  // CMP aggiorna solo i flag
        write_operand(ins.dst, ins.width, result);
    }
}

void Cpu::exec_inc(const Instruction& ins) {
    write_operand(ins.dst, ins.width, alu_inc(ins.width, read_operand(ins.dst, ins.width), regs_.flags));
}

void Cpu::exec_dec(const Instruction& ins) {
    write_operand(ins.dst, ins.width, alu_dec(ins.width, read_operand(ins.dst, ins.width), regs_.flags));
}

void Cpu::exec_neg(const Instruction& ins) {
    write_operand(ins.dst, ins.width, alu_neg(ins.width, read_operand(ins.dst, ins.width), regs_.flags));
}

void Cpu::exec_not(const Instruction& ins) {
    write_operand(ins.dst, ins.width, alu_not(ins.width, read_operand(ins.dst, ins.width)));
}

void Cpu::exec_nop(const Instruction&) {}

// IP è già avanzato oltre F4: è il comportamento dell'8086, che dopo un'interruzione
// riprende dall'istruzione successiva a HLT. La suite non copre HLT: si verifica a mano.
void Cpu::exec_hlt(const Instruction&) {
    halted_ = true;
}

// L'8086 decrementa SP prima di leggere l'operando: PUSH SP salva quindi il valore già
// decrementato. Dall'80286 in poi si salva invece il valore precedente.
void Cpu::exec_push(const Instruction& ins) {
    decrement_sp();
    memory_.write16(regs_.get(SegReg::SS), regs_.get(Reg16::SP), read_operand(ins.dst, Width::Word));
}

// SP viene incrementato prima di scrivere la destinazione: con POP SP vince il valore letto.
void Cpu::exec_pop(const Instruction& ins) {
    write_operand(ins.dst, Width::Word, pop_word());
}

// Per i salti relativi l'operando è la destinazione già calcolata dal decoder; per JMP r/m16
// è il valore del registro o della parola in memoria.
void Cpu::exec_jmp(const Instruction& ins) {
    regs_.ip = read_operand(ins.dst, Width::Word);
}

void Cpu::exec_jz(const Instruction& ins) {
    if (regs_.flags.test(Flag::ZF)) {
        regs_.ip = read_operand(ins.dst, Width::Word);
    }
}

void Cpu::exec_jnz(const Instruction& ins) {
    if (!regs_.flags.test(Flag::ZF)) {
        regs_.ip = read_operand(ins.dst, Width::Word);
    }
}

// La destinazione si legge prima del push dell'indirizzo di ritorno: conta per CALL SP e per
// CALL [mem] quando la parola in memoria è sullo stack.
void Cpu::exec_call(const Instruction& ins) {
    const std::uint16_t target = read_operand(ins.dst, Width::Word);
    push_word(regs_.ip);  // IP punta già all'istruzione successiva
    regs_.ip = target;
}

void Cpu::exec_ret(const Instruction&) {
    regs_.ip = pop_word();
}

}  // namespace emu
