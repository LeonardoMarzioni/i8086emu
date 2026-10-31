#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/decoder.hpp"
#include "core/registers.hpp"

using namespace emu;

namespace {

Instruction decode_bytes(const std::vector<std::uint8_t>& bytes) {
    Memory mem;
    mem.load(physical_address(0x1000, 0x0100), bytes);
    return decode(mem, 0x1000, 0x0100);
}

constexpr std::uint8_t code(Reg8 r) { return static_cast<std::uint8_t>(r); }
constexpr std::uint8_t code(Reg16 r) { return static_cast<std::uint8_t>(r); }

void check_reg(const Operand& op, Reg8 r) {
    CHECK(op.type == Operand::Type::Reg8);
    CHECK(op.reg == code(r));
}

void check_reg(const Operand& op, Reg16 r) {
    CHECK(op.type == Operand::Type::Reg16);
    CHECK(op.reg == code(r));
}

void check_imm(const Operand& op, std::uint16_t value) {
    CHECK(op.type == Operand::Type::Imm);
    CHECK(op.imm == value);
}

}  // namespace

TEST_CASE("la tabella coincide con la mappa opcode Intel", "[decoder]") {
    using enum Mnemonic;
    using enum OperandKind;
    struct Case {
        std::uint8_t opcode;
        Mnemonic mnemonic;
        OperandKind dst;
        OperandKind src;
    };
    // Voci scritte a mano dalla mappa opcode, indipendenti dal ciclo che costruisce la tabella.
    const Case cases[] = {
        {0x00, Add, Eb, Gb}, {0x0D, Or, AX, Iv},  {0x22, And, Gb, Eb}, {0x2B, Sub, Gv, Ev},
        {0x34, Xor, AL, Ib}, {0x39, Cmp, Ev, Gv}, {0x43, Inc, Zv, None}, {0x4F, Dec, Zv, None},
        {0x83, Group1, Ev, Ibs}, {0x8A, Mov, Gb, Eb}, {0xB5, Mov, Zb, Ib}, {0xBE, Mov, Zv, Iv},
        {0xC7, Group11, Ev, Iv}, {0xF6, Group3, Eb, None},
        {0x8C, Mov, Ew, Sw}, {0x8E, Mov, Sw, Ew}, {0xA0, Mov, AL, Ob}, {0xA3, Mov, Ov, AX},
        {0x26, SegmentPrefix, None, None}, {0x3E, SegmentPrefix, None, None},
        {0x82, Group82, Eb, Ib},
    };
    for (const Case& c : cases) {
        INFO("opcode " << int{c.opcode});
        const OpcodeInfo& info = kOpcodeTable[c.opcode];
        CHECK(info.mnemonic == c.mnemonic);
        CHECK(info.dst == c.dst);
        CHECK(info.src == c.src);
    }
    // Fuori dal sottoinsieme: ADC (10-15), SBB (18-1D), PUSH ES (06).
    const std::uint8_t outside[] = {0x10, 0x15, 0x18, 0x1D, 0x06};
    for (const std::uint8_t opcode : outside) {
        INFO("opcode " << int{opcode});
        CHECK(kOpcodeTable[opcode].mnemonic == Unimplemented);
    }
}

TEST_CASE("MOV registro, immediato (B8+r)", "[decoder]") {
    const Instruction ins = decode_bytes({0xBB, 0xCD, 0xAB});  // MOV BX, ABCDh
    CHECK(ins.mnemonic == Mnemonic::Mov);
    CHECK(ins.length == 3);
    CHECK(ins.width == Width::Word);
    check_reg(ins.dst, Reg16::BX);
    check_imm(ins.src, 0xABCD);
}

TEST_CASE("il bit d: 88 C4 e 8A E0 sono la stessa istruzione MOV AH, AL", "[decoder]") {
    for (const auto& bytes : {std::vector<std::uint8_t>{0x88, 0xC4}, std::vector<std::uint8_t>{0x8A, 0xE0}}) {
        const Instruction ins = decode_bytes(bytes);
        CHECK(ins.mnemonic == Mnemonic::Mov);
        CHECK(ins.length == 2);
        CHECK(ins.width == Width::Byte);
        check_reg(ins.dst, Reg8::AH);
        check_reg(ins.src, Reg8::AL);
    }
}

TEST_CASE("forme accumulatore-immediato", "[decoder]") {
    const Instruction add8 = decode_bytes({0x04, 0x10});  // ADD AL, 10h
    CHECK(add8.mnemonic == Mnemonic::Add);
    CHECK(add8.length == 2);
    check_reg(add8.dst, Reg8::AL);
    check_imm(add8.src, 0x10);

    const Instruction cmp16 = decode_bytes({0x3D, 0x34, 0x12});  // CMP AX, 1234h
    CHECK(cmp16.mnemonic == Mnemonic::Cmp);
    CHECK(cmp16.length == 3);
    CHECK(cmp16.width == Width::Word);
    check_reg(cmp16.dst, Reg16::AX);
    check_imm(cmp16.src, 0x1234);
}

TEST_CASE("gruppo 1: il campo reg sceglie l'operazione", "[decoder]") {
    const Instruction sub = decode_bytes({0x81, 0xEB, 0x00, 0x01});  // SUB BX, 0100h
    CHECK(sub.mnemonic == Mnemonic::Sub);
    CHECK(sub.group_reg == 5);
    CHECK(sub.length == 4);
    check_reg(sub.dst, Reg16::BX);
    check_imm(sub.src, 0x0100);

    const Instruction add8 = decode_bytes({0x80, 0xC3, 0x10});  // ADD BL, 10h
    CHECK(add8.mnemonic == Mnemonic::Add);
    CHECK(add8.width == Width::Byte);
    check_reg(add8.dst, Reg8::BL);
}

TEST_CASE("82: solo le operazioni documentate dal manuale", "[decoder]") {
    const Instruction add = decode_bytes({0x82, 0xC3, 0x10});  // ADD BL, 10h
    CHECK(add.mnemonic == Mnemonic::Add);
    CHECK(add.length == 3);
    CHECK(add.width == Width::Byte);
    check_reg(add.dst, Reg8::BL);
    check_imm(add.src, 0x10);

    CHECK(decode_bytes({0x82, 0xE8, 0x01}).mnemonic == Mnemonic::Sub);  // SUB AL, 1
    CHECK(decode_bytes({0x82, 0xF8, 0x01}).mnemonic == Mnemonic::Cmp);  // CMP AL, 1

    // /1, /4, /6 "not used" nella Tabella 4-13 (l'hardware li esegue come 80)
    for (const std::uint8_t modrm : {std::uint8_t{0xC8}, std::uint8_t{0xE0}, std::uint8_t{0xF0}}) {
        const Instruction ins = decode_bytes({0x82, modrm, 0x01});
        CHECK(ins.mnemonic == Mnemonic::Unimplemented);
        CHECK(ins.group_reg == ((modrm >> 3) & 7));
    }
}

TEST_CASE("83: immediato a 8 bit esteso con segno", "[decoder]") {
    const Instruction neg = decode_bytes({0x83, 0xC0, 0xFF});  // ADD AX, -1
    CHECK(neg.mnemonic == Mnemonic::Add);
    CHECK(neg.length == 3);
    CHECK(neg.width == Width::Word);
    check_imm(neg.src, 0xFFFF);

    const Instruction pos = decode_bytes({0x83, 0xE3, 0x7F});  // AND BX, 007Fh
    CHECK(pos.mnemonic == Mnemonic::And);
    check_reg(pos.dst, Reg16::BX);
    check_imm(pos.src, 0x007F);
}

TEST_CASE("gruppi a un operando: NEG, NOT, INC, DEC", "[decoder]") {
    const Instruction neg = decode_bytes({0xF6, 0xD8});  // NEG AL
    CHECK(neg.mnemonic == Mnemonic::Neg);
    CHECK(neg.length == 2);
    check_reg(neg.dst, Reg8::AL);
    CHECK(neg.src.type == Operand::Type::None);

    const Instruction not16 = decode_bytes({0xF7, 0xD3});  // NOT BX
    CHECK(not16.mnemonic == Mnemonic::Not);
    check_reg(not16.dst, Reg16::BX);

    const Instruction inc8 = decode_bytes({0xFE, 0xC4});  // INC AH
    CHECK(inc8.mnemonic == Mnemonic::Inc);
    check_reg(inc8.dst, Reg8::AH);

    const Instruction dec16 = decode_bytes({0xFF, 0xC9});  // DEC CX
    CHECK(dec16.mnemonic == Mnemonic::Dec);
    check_reg(dec16.dst, Reg16::CX);
}

TEST_CASE("C6/C7 sono MOV solo con reg = 0", "[decoder]") {
    const Instruction mov = decode_bytes({0xC6, 0xC0, 0x42});  // MOV AL, 42h
    CHECK(mov.mnemonic == Mnemonic::Mov);
    CHECK(mov.length == 3);
    check_reg(mov.dst, Reg8::AL);
    check_imm(mov.src, 0x42);

    const Instruction bad = decode_bytes({0xC6, 0xC8, 0x42});
    CHECK(bad.mnemonic == Mnemonic::Unimplemented);
    CHECK(bad.group_reg == 1);
}

TEST_CASE("opcode non implementati", "[decoder]") {
    const Instruction adc = decode_bytes({0x10, 0xC0});  // ADC AL, AL
    CHECK(adc.mnemonic == Mnemonic::Unimplemented);
    CHECK(adc.opcode == 0x10);
    CHECK_FALSE(adc.group_reg.has_value());

    const Instruction adc_group = decode_bytes({0x80, 0xD0, 0x05});  // ADC AL, 5
    CHECK(adc_group.mnemonic == Mnemonic::Unimplemented);
    CHECK(adc_group.opcode == 0x80);
    CHECK(adc_group.group_reg == 2);
}

TEST_CASE("NOP e HLT occupano un byte", "[decoder]") {
    CHECK(decode_bytes({0x90}).mnemonic == Mnemonic::Nop);
    CHECK(decode_bytes({0x90}).length == 1);
    CHECK(decode_bytes({0xF4}).mnemonic == Mnemonic::Hlt);
    CHECK(decode_bytes({0xF4}).length == 1);
}

TEST_CASE("la lettura dei byte fa wrap a 16 bit dentro CS", "[decoder]") {
    Memory mem;
    mem.write8(0x1000, 0xFFFF, 0xB0);  // MOV AL, imm8 ...
    mem.write8(0x1000, 0x0000, 0x42);  // ... con l'immediato a 1000:0000
    const Instruction ins = decode(mem, 0x1000, 0xFFFF);
    CHECK(ins.mnemonic == Mnemonic::Mov);
    CHECK(ins.length == 2);
    check_imm(ins.src, 0x42);
}

// --- Operandi in memoria e prefissi ---

namespace {

void check_mem(const Operand& op, std::optional<Reg16> base, std::optional<Reg16> index,
               std::uint16_t displacement, std::uint8_t displacement_size, SegReg segment) {
    REQUIRE(op.type == Operand::Type::Mem);
    CHECK(op.mem.base == base);
    CHECK(op.mem.index == index);
    CHECK(op.mem.displacement == displacement);
    CHECK(op.mem.displacement_size == displacement_size);
    CHECK(op.mem.segment == segment);
}

}  // namespace

TEST_CASE("Tabella 4-10: gli otto valori di r/m con mod = 00", "[decoder][memory]") {
    struct Case {
        std::uint8_t rm;
        std::optional<Reg16> base;
        std::optional<Reg16> index;
        SegReg segment;  // SS quando la base è BP
    };
    const Case cases[] = {
        {0, Reg16::BX, Reg16::SI, SegReg::DS},   {1, Reg16::BX, Reg16::DI, SegReg::DS},
        {2, Reg16::BP, Reg16::SI, SegReg::SS},   {3, Reg16::BP, Reg16::DI, SegReg::SS},
        {4, std::nullopt, Reg16::SI, SegReg::DS}, {5, std::nullopt, Reg16::DI, SegReg::DS},
        {7, Reg16::BX, std::nullopt, SegReg::DS},
    };
    for (const Case& c : cases) {
        INFO("r/m " << int{c.rm});
        const Instruction ins = decode_bytes({0x8B, c.rm});  // MOV AX, [...]
        CHECK(ins.mnemonic == Mnemonic::Mov);
        CHECK(ins.length == 2);
        check_reg(ins.dst, Reg16::AX);
        check_mem(ins.src, c.base, c.index, 0, 0, c.segment);
    }
}

TEST_CASE("mod = 00 con r/m = 110 è un indirizzo diretto, non [BP]", "[decoder][memory]") {
    const Instruction ins = decode_bytes({0x8B, 0x06, 0x34, 0x12});  // MOV AX, [1234h]
    CHECK(ins.length == 4);
    check_mem(ins.src, std::nullopt, std::nullopt, 0x1234, 2, SegReg::DS);

    const Instruction bp = decode_bytes({0x8B, 0x46, 0x00});  // MOV AX, [BP+0]
    CHECK(bp.length == 3);
    check_mem(bp.src, Reg16::BP, std::nullopt, 0x0000, 1, SegReg::SS);
}

TEST_CASE("spiazzamento a 8 bit esteso con segno e a 16 bit", "[decoder][memory]") {
    const Instruction d8 = decode_bytes({0x8B, 0x46, 0xFE});  // MOV AX, [BP-2]
    CHECK(d8.length == 3);
    check_mem(d8.src, Reg16::BP, std::nullopt, 0xFFFE, 1, SegReg::SS);

    const Instruction d16 = decode_bytes({0x8B, 0x87, 0x34, 0x12});  // MOV AX, [BX+1234h]
    CHECK(d16.length == 4);
    check_mem(d16.src, Reg16::BX, std::nullopt, 0x1234, 2, SegReg::DS);
}

TEST_CASE("l'immediato segue lo spiazzamento", "[decoder][memory]") {
    const Instruction mov = decode_bytes({0xC7, 0x47, 0x02, 0x34, 0x12});  // MOV WORD [BX+2], 1234h
    CHECK(mov.mnemonic == Mnemonic::Mov);
    CHECK(mov.length == 5);
    CHECK(mov.width == Width::Word);
    check_mem(mov.dst, Reg16::BX, std::nullopt, 0x0002, 1, SegReg::DS);
    check_imm(mov.src, 0x1234);

    // Istruzione più lunga senza prefissi: opcode, ModR/M, disp16, imm16.
    const Instruction add = decode_bytes({0x81, 0x86, 0x00, 0x01, 0xFF, 0x00});  // ADD WORD [BP+100h], FFh
    CHECK(add.mnemonic == Mnemonic::Add);
    CHECK(add.length == 6);
    check_mem(add.dst, Reg16::BP, std::nullopt, 0x0100, 2, SegReg::SS);
    check_imm(add.src, 0x00FF);

    const Instruction cmp = decode_bytes({0x83, 0x7E, 0xFC, 0x05});  // CMP WORD [BP-4], 5
    CHECK(cmp.mnemonic == Mnemonic::Cmp);
    CHECK(cmp.length == 4);
    check_mem(cmp.dst, Reg16::BP, std::nullopt, 0xFFFC, 1, SegReg::SS);
    check_imm(cmp.src, 0x0005);
}

TEST_CASE("il prefisso sostituisce il segmento predefinito", "[decoder][prefix]") {
    const Instruction es = decode_bytes({0x26, 0x8B, 0x07});  // MOV AX, ES:[BX]
    CHECK(es.mnemonic == Mnemonic::Mov);
    CHECK(es.opcode == 0x8B);
    CHECK(es.length == 3);
    CHECK(es.segment_override == SegReg::ES);
    check_mem(es.src, Reg16::BX, std::nullopt, 0, 0, SegReg::ES);

    const Instruction ds = decode_bytes({0x3E, 0x8B, 0x46, 0x00});  // MOV AX, DS:[BP+0]
    check_mem(ds.src, Reg16::BP, std::nullopt, 0, 1, SegReg::DS);
}

TEST_CASE("con più prefissi vale l'ultimo", "[decoder][prefix]") {
    // Convenzione dell'emulatore: la suite non contiene istruzioni con più di un prefisso.
    const Instruction ins = decode_bytes({0x26, 0x2E, 0x8B, 0x07});  // MOV AX, CS:[BX]
    CHECK(ins.length == 4);
    CHECK(ins.segment_override == SegReg::CS);
    check_mem(ins.src, Reg16::BX, std::nullopt, 0, 0, SegReg::CS);
}

TEST_CASE("un prefisso su un'istruzione senza memoria allunga solo l'istruzione", "[decoder][prefix]") {
    const Instruction ins = decode_bytes({0x26, 0xB8, 0x34, 0x12});  // ES: MOV AX, 1234h
    CHECK(ins.mnemonic == Mnemonic::Mov);
    CHECK(ins.length == 4);
    check_reg(ins.dst, Reg16::AX);
    check_imm(ins.src, 0x1234);
}

TEST_CASE("oltre kMaxPrefixes prefissi l'istruzione non è implementata", "[decoder][prefix]") {
    std::vector<std::uint8_t> bytes(kMaxPrefixes, 0x26);
    bytes.push_back(0x90);
    const Instruction ok = decode_bytes(bytes);
    CHECK(ok.mnemonic == Mnemonic::Nop);
    CHECK(ok.length == kMaxPrefixes + 1);

    bytes.back() = 0x26;  // un prefisso in più
    bytes.push_back(0x90);
    const Instruction too_many = decode_bytes(bytes);
    CHECK(too_many.mnemonic == Mnemonic::Unimplemented);
    CHECK(too_many.opcode == 0x26);
}

TEST_CASE("A0-A3: accumulatore e indirizzo diretto", "[decoder][memory]") {
    const Instruction load = decode_bytes({0xA1, 0x34, 0x12});  // MOV AX, [1234h]
    CHECK(load.mnemonic == Mnemonic::Mov);
    CHECK(load.length == 3);
    CHECK(load.width == Width::Word);
    check_reg(load.dst, Reg16::AX);
    check_mem(load.src, std::nullopt, std::nullopt, 0x1234, 2, SegReg::DS);

    const Instruction store = decode_bytes({0x26, 0xA2, 0x00, 0x01});  // MOV ES:[0100h], AL
    CHECK(store.length == 4);
    CHECK(store.width == Width::Byte);
    check_mem(store.dst, std::nullopt, std::nullopt, 0x0100, 2, SegReg::ES);
    check_reg(store.src, Reg8::AL);
}

TEST_CASE("8C/8E: MOV con i registri di segmento", "[decoder][memory]") {
    const Instruction from_seg = decode_bytes({0x8C, 0xD8});  // MOV AX, DS
    CHECK(from_seg.mnemonic == Mnemonic::Mov);
    CHECK(from_seg.width == Width::Word);
    check_reg(from_seg.dst, Reg16::AX);
    CHECK(from_seg.src.type == Operand::Type::Seg);
    CHECK(from_seg.src.reg == static_cast<std::uint8_t>(SegReg::DS));

    const Instruction to_seg = decode_bytes({0x8E, 0x1E, 0x34, 0x12});  // MOV DS, [1234h]
    CHECK(to_seg.length == 4);
    CHECK(to_seg.dst.type == Operand::Type::Seg);
    CHECK(to_seg.dst.reg == static_cast<std::uint8_t>(SegReg::DS));
    check_mem(to_seg.src, std::nullopt, std::nullopt, 0x1234, 2, SegReg::DS);

    const Instruction from_cs = decode_bytes({0x8C, 0xC8});  // MOV AX, CS: ammessa
    CHECK(from_cs.mnemonic == Mnemonic::Mov);
}

// --- Stack e trasferimenti di controllo ---

namespace {

void check_target(const Operand& op, std::uint16_t target) {
    CHECK(op.type == Operand::Type::Target);
    CHECK(op.imm == target);
}

Instruction decode_at(std::uint16_t ip, const std::vector<std::uint8_t>& bytes) {
    Memory mem;
    mem.load(physical_address(0x1000, ip), bytes);
    return decode(mem, 0x1000, ip);
}

}  // namespace

TEST_CASE("PUSH e POP di un registro a 16 bit", "[decoder][stack]") {
    const Instruction push = decode_bytes({0x50});  // PUSH AX
    CHECK(push.mnemonic == Mnemonic::Push);
    CHECK(push.length == 1);
    check_reg(push.dst, Reg16::AX);

    const Instruction pop = decode_bytes({0x5F});  // POP DI
    CHECK(pop.mnemonic == Mnemonic::Pop);
    check_reg(pop.dst, Reg16::DI);
}

TEST_CASE("i salti relativi partono dall'IP dell'istruzione successiva", "[decoder][jump]") {
    // Le istruzioni stanno a 1000:0100.
    const Instruction self = decode_bytes({0xEB, 0xFE});  // JMP $: 0102h - 2
    CHECK(self.mnemonic == Mnemonic::Jmp);
    CHECK(self.length == 2);
    check_target(self.dst, 0x0100);

    check_target(decode_bytes({0xE9, 0xFD, 0xFF}).dst, 0x0100);  // JMP near: 0103h - 3
    check_target(decode_bytes({0x74, 0x05}).dst, 0x0107);        // JZ: 0102h + 5
    check_target(decode_bytes({0x75, 0xF0}).dst, 0x00F2);        // JNZ: 0102h - 16
    CHECK(decode_bytes({0x75, 0xF0}).mnemonic == Mnemonic::Jnz);

    const Instruction call = decode_bytes({0xE8, 0x00, 0x00});  // CALL all'istruzione dopo
    CHECK(call.mnemonic == Mnemonic::Call);
    CHECK(call.length == 3);
    check_target(call.dst, 0x0103);
}

TEST_CASE("la destinazione fa wrap a 16 bit e conta i prefissi", "[decoder][jump]") {
    check_target(decode_at(0xFFF0, {0xEB, 0x7F}).dst, 0x0071);  // FFF2h + 7Fh
    check_target(decode_at(0x0010, {0xE9, 0x00, 0xFF}).dst, 0xFF13);  // 0013h - 100h

    const Instruction prefixed = decode_bytes({0x26, 0xEB, 0x00});  // ES: JMP all'istruzione dopo
    CHECK(prefixed.length == 3);
    check_target(prefixed.dst, 0x0103);
}

TEST_CASE("CALL e JMP indirette nel gruppo 5", "[decoder][jump]") {
    const Instruction call_reg = decode_bytes({0xFF, 0xD3});  // CALL BX
    CHECK(call_reg.mnemonic == Mnemonic::Call);
    CHECK(call_reg.group_reg == 2);
    CHECK(call_reg.width == Width::Word);
    check_reg(call_reg.dst, Reg16::BX);

    const Instruction jmp_mem = decode_bytes({0xFF, 0x67, 0x02});  // JMP [BX+2]
    CHECK(jmp_mem.mnemonic == Mnemonic::Jmp);
    CHECK(jmp_mem.length == 3);
    check_mem(jmp_mem.dst, Reg16::BX, std::nullopt, 0x0002, 1, SegReg::DS);

    // Forme far e PUSH r/m16: fuori dal sottoinsieme
    CHECK(decode_bytes({0xFF, 0x1F}).mnemonic == Mnemonic::Unimplemented);  // CALL FAR [BX]
    CHECK(decode_bytes({0xFF, 0x2F}).mnemonic == Mnemonic::Unimplemented);  // JMP FAR [BX]
    CHECK(decode_bytes({0xFF, 0x37}).mnemonic == Mnemonic::Unimplemented);  // PUSH [BX]
}

TEST_CASE("RET near senza operandi", "[decoder][jump]") {
    const Instruction ret = decode_bytes({0xC3});
    CHECK(ret.mnemonic == Mnemonic::Ret);
    CHECK(ret.length == 1);
    CHECK(decode_bytes({0xC2, 0x02, 0x00}).mnemonic == Mnemonic::Unimplemented);  // RET imm16
}

TEST_CASE("8E con CS come destinazione e reg 4-7 non sono implementati", "[decoder][memory]") {
    CHECK(decode_bytes({0x8E, 0xC8}).mnemonic == Mnemonic::Unimplemented);  // MOV CS, AX
    CHECK(decode_bytes({0x8C, 0xE0}).mnemonic == Mnemonic::Unimplemented);  // reg = 4
    CHECK(decode_bytes({0x8E, 0xF8}).mnemonic == Mnemonic::Unimplemented);  // reg = 7
}
