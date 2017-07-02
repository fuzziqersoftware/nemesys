#include "AMD64Assembler.hh"

#include <string>

using namespace std;


MemoryReference::MemoryReference(Register base_register, int64_t offset,
    Register index_register, uint8_t field_size) : base_register(base_register),
    index_register(index_register), field_size(field_size), offset(offset) { }
MemoryReference::MemoryReference(Register base_register) :
    base_register(base_register), index_register(Register::None), field_size(0),
    offset(0) { }

static inline bool is_extension_register(Register r) {
  int8_t rn = static_cast<int8_t>(r);
  return rn >= 8;
}



static string generate_rm(Operation op, const MemoryReference& mem,
    Register reg, OperandSize size) {
  uint16_t opcode = static_cast<uint16_t>(op);

  string ret;
  if (!mem.field_size) { // behavior = 3 (register reference)
    bool mem_ext = is_extension_register(mem.base_register);
    bool reg_ext = is_extension_register(reg);

    uint8_t prefix_byte = 0x40 | (mem_ext ? 0x01 : 0) | (reg_ext ? 0x04 : 0);
    if (size == OperandSize::QuadWord) {
      prefix_byte |= 0x08;
      opcode |= 0x01;
    } else if (size == OperandSize::DoubleWord) {
      opcode |= 0x01;
    } else if (size == OperandSize::Word) {
      ret += 0x66;
      opcode |= 0x01;
    }

    if (prefix_byte != 0x40) {
      ret += prefix_byte;
    }
    if (opcode > 0xFF) {
      ret += (opcode >> 8);
    }
    ret += (opcode & 0xFF);
    ret += static_cast<char>(0xC0 | ((reg & 7) << 3) | (mem.base_register & 7));
    return ret;
  }

  // TODO: we currently don't implement the SIB byte
  if (mem.base_register == Register::None) {
    throw invalid_argument("memory references without base not supported");
  }
  if ((mem.base_register == Register::RSP) ||
      (mem.base_register == Register::R12)) {
    throw invalid_argument("scaled index references not supported");
  }
  if (mem.index_register != Register::None) {
    throw invalid_argument("indexed memory references not supported");
  }

  bool reg_ext = is_extension_register(reg);
  bool mem_index_ext = false; // TODO
  bool mem_base_ext = is_extension_register(mem.base_register);

  uint8_t rm_byte = ((reg & 7) << 3) | (mem.base_register & 7);

  // if an offset was given, update the behavior appropriately
  if (mem.offset == 0) {
    // behavior is 0; nothing to do
  } else if ((mem.offset <= 0x7F) && (mem.offset >= -0x80)) {
    rm_byte |= 0x40;
  } else if ((mem.offset <= 0x7FFFFFFFLL) && (mem.offset >= -0x80000000LL)) {
    rm_byte |= 0x80;
  } else {
    throw invalid_argument("offset must fit in 32 bits");
  }

  // if no offset was given and the sib byte was not used and the base reg is
  // RSP, then add a fake offset of 0 to the opcode (this is because this case
  // is shadowed by a special case above)
  if ((mem.offset == 0) && ((rm_byte & 7) != 0x04) &&
      (mem.base_register == Register::RSP)) {
    rm_byte |= 0x40;
  }

  // fill in the ret string
  uint8_t prefix_byte = 0x40 | (reg_ext ? 0x04 : 0) | (mem_index_ext ? 0x02 : 0) |
      (mem_base_ext ? 0x01 : 0);
  if (size == OperandSize::QuadWord) {
    prefix_byte |= 0x08;
    opcode |= 0x01;
  } else if (size == OperandSize::DoubleWord) {
    opcode |= 0x01;
  } else if (size == OperandSize::Word) {
    ret += 0x66;
    opcode |= 0x01;
  }

  if (prefix_byte != 0x40) {
    ret += prefix_byte;
  }

  if (opcode > 0xFF) {
    ret += (opcode >> 8);
  }
  ret += (opcode & 0xFF);

  ret += rm_byte;
  if (rm_byte & 0x40) {
    ret.append(reinterpret_cast<const char*>(&mem.offset), 1);
  } else if (rm_byte & 0x80) {
    ret.append(reinterpret_cast<const char*>(&mem.offset), 4);
  }
  return ret;
}

static inline string generate_rm(Operation op, const MemoryReference& mem,
    uint8_t z, OperandSize size) {
  return generate_rm(op, mem, static_cast<Register>(z), size);
}



Operation load_store_oper_for_args(Operation op, const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  return static_cast<Operation>(op | ((size != OperandSize::Byte) ? 1 : 0) |
      (from.field_size ? 2 : 0));
}

string generate_load_store(Operation base_op, const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  if (to.field_size && from.field_size) {
    throw invalid_argument("load/store opcodes can have at most one memory reference");
  }

  Operation op = load_store_oper_for_args(Operation::MOV_STORE8, to, from, size);
  if (!from.field_size) {
    return generate_rm(op, to, from.base_register, size);
  }
  return generate_rm(op, from, to.base_register, size);
}



string generate_push(Register r) {
  string ret;
  if (is_extension_register(r)) {
    ret += '\x41';
    ret += static_cast<char>(r) - 8 + 0x50;
  } else {
    ret += static_cast<char>(r) + 0x50;
  }
  return ret;
}

string generate_pop(Register r) {
  string ret;
  if (is_extension_register(r)) {
    ret += '\x41';
    ret += static_cast<char>(r) - 8 + 0x58;
  } else {
    ret += static_cast<char>(r) + 0x58;
  }
  return ret;
}



string generate_mov(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::MOV_STORE8, to, from, size);
}

string generate_mov(Register reg, int64_t value, OperandSize size) {
  if (value == 0) {
    // xor reg, reg
    MemoryReference r(reg);
    return generate_xor(r, r, size);
  }

  if (size == OperandSize::QuadWord) {
    // TODO: we can optimize for code size by not using movabs for small values,
    // but for now I'm lazy
    string ret;
    ret += 0x48 | (is_extension_register(reg) ? 0x01 : 0);
    ret += 0xB8 | (reg & 7);
    ret.append(reinterpret_cast<const char*>(&value), 8);
    return ret;

  } else if (size == OperandSize::DoubleWord) {
    string ret;
    if (is_extension_register(reg)) {
      ret += 0x41;
    }
    ret += 0xB8 | (reg & 7);
    ret.append(reinterpret_cast<const char*>(&value), 4);
    return ret;

  } else if (size == OperandSize::Word) {
    string ret;
    ret += 0x66;
    if (is_extension_register(reg)) {
      ret += 0x41;
    }
    ret += 0xB8 | (reg & 7);
    ret.append(reinterpret_cast<const char*>(&value), 2);
    return ret;

  } else if (size == OperandSize::Byte) {
    string ret;
    if (is_extension_register(reg)) {
      ret += 0x41;
    }
    ret += 0xB0 | (reg & 7);
    ret += static_cast<int8_t>(value);
    return ret;
  } else {
    throw invalid_argument("unknown operand size");
  }
}



string generate_jmp(const MemoryReference& mem) {
  return generate_rm(Operation::CALL_JMP_ABS, mem, 4, OperandSize::DoubleWord);
}

string generate_jmp(int64_t offset) {
  string ret;
  if ((offset > 0x7FFFFFFFLL) || (offset < -0x80000000LL)) {
    throw invalid_argument("jump offset too large");
  } else if ((offset > 0x7F) || (offset < -0x80)) {
    ret += 0xE9;
    ret.append(reinterpret_cast<const char*>(&offset), 4);
  } else {
    ret += 0xEB;
    ret.append(reinterpret_cast<const char*>(&offset), 1);
  }
  return ret;
}

string generate_call(const MemoryReference& mem) {
  return generate_rm(Operation::CALL_JMP_ABS, mem, 2, OperandSize::DoubleWord);
}

string generate_call(int64_t offset) {
  string ret;
  ret += 0xE8;
  ret.append(reinterpret_cast<const char*>(&offset), 4);
  return ret;
}

string generate_ret(uint16_t stack_bytes) {
  if (stack_bytes) {
    string ret("\xC2", 1);
    ret.append(reinterpret_cast<const char*>(&stack_bytes), 2);
    return ret;
  } else {
    return string("\xC3", 1);
  }
}



static string generate_jcc(Operation op8, Operation op, int64_t offset) {
  string ret;
  if ((offset > 0x7FFFFFFFLL) || (offset < -0x80000000LL)) {
    throw invalid_argument("jump offset too large");
  } else if ((offset > 0x7F) || (offset < -0x80)) {
    if (op > 0xFF) {
      ret += (op >> 8);
    }
    ret += (op & 0xFF);
    ret.append(reinterpret_cast<const char*>(&offset), 4);
  } else {
    if (op8 > 0xFF) {
      ret += (op8 >> 8);
    }
    ret += (op8 & 0xFF);
    ret += static_cast<int8_t>(offset);
  }
  return ret;
}

std::string generate_jo(int64_t offset) {
  return generate_jcc(Operation::JO8, Operation::JO, offset);
}

std::string generate_jno(int64_t offset) {
  return generate_jcc(Operation::JNO8, Operation::JNO, offset);
}

std::string generate_jb(int64_t offset) {
  return generate_jcc(Operation::JB8, Operation::JB, offset);
}

std::string generate_jnae(int64_t offset) {
  return generate_jcc(Operation::JNAE8, Operation::JNAE, offset);
}

std::string generate_jc(int64_t offset) {
  return generate_jcc(Operation::JC8, Operation::JC, offset);
}

std::string generate_jnb(int64_t offset) {
  return generate_jcc(Operation::JNB8, Operation::JNB, offset);
}

std::string generate_jae(int64_t offset) {
  return generate_jcc(Operation::JAE8, Operation::JAE, offset);
}

std::string generate_jnc(int64_t offset) {
  return generate_jcc(Operation::JNC8, Operation::JNC, offset);
}

std::string generate_jz(int64_t offset) {
  return generate_jcc(Operation::JZ8, Operation::JZ, offset);
}

std::string generate_je(int64_t offset) {
  return generate_jcc(Operation::JE8, Operation::JE, offset);
}

std::string generate_jnz(int64_t offset) {
  return generate_jcc(Operation::JNZ8, Operation::JNZ, offset);
}

std::string generate_jne(int64_t offset) {
  return generate_jcc(Operation::JNE8, Operation::JNE, offset);
}

std::string generate_jbe(int64_t offset) {
  return generate_jcc(Operation::JBE8, Operation::JBE, offset);
}

std::string generate_jna(int64_t offset) {
  return generate_jcc(Operation::JNA8, Operation::JNA, offset);
}

std::string generate_jnbe(int64_t offset) {
  return generate_jcc(Operation::JNBE8, Operation::JNBE, offset);
}

std::string generate_ja(int64_t offset) {
  return generate_jcc(Operation::JA8, Operation::JA, offset);
}

std::string generate_js(int64_t offset) {
  return generate_jcc(Operation::JS8, Operation::JS, offset);
}

std::string generate_jns(int64_t offset) {
  return generate_jcc(Operation::JNS8, Operation::JNS, offset);
}

std::string generate_jp(int64_t offset) {
  return generate_jcc(Operation::JP8, Operation::JP, offset);
}

std::string generate_jpe(int64_t offset) {
  return generate_jcc(Operation::JPE8, Operation::JPE, offset);
}

std::string generate_jnp(int64_t offset) {
  return generate_jcc(Operation::JNP8, Operation::JNP, offset);
}

std::string generate_jpo(int64_t offset) {
  return generate_jcc(Operation::JPO8, Operation::JPO, offset);
}

std::string generate_jl(int64_t offset) {
  return generate_jcc(Operation::JL8, Operation::JL, offset);
}

std::string generate_jnge(int64_t offset) {
  return generate_jcc(Operation::JNGE8, Operation::JNGE, offset);
}

std::string generate_jnl(int64_t offset) {
  return generate_jcc(Operation::JNL8, Operation::JNL, offset);
}

std::string generate_jge(int64_t offset) {
  return generate_jcc(Operation::JGE8, Operation::JGE, offset);
}

std::string generate_jle(int64_t offset) {
  return generate_jcc(Operation::JLE8, Operation::JLE, offset);
}

std::string generate_jng(int64_t offset) {
  return generate_jcc(Operation::JNG8, Operation::JNG, offset);
}

std::string generate_jnle(int64_t offset) {
  return generate_jcc(Operation::JNLE8, Operation::JNLE, offset);
}

std::string generate_jg(int64_t offset) {
  return generate_jcc(Operation::JG8, Operation::JG, offset);
}





static string generate_imm_math(Operation math_op, const MemoryReference& to,
    int64_t value, OperandSize size) {
  if (math_op & 0xC7) {
    throw invalid_argument("immediate math opcodes must use basic Operation types");
  }

  Operation op;
  if (size == OperandSize::Byte) {
    op = Operation::MATH8_IMM8;
  } else if ((value > 0x7FFFFFFFLL) || (value < -0x80000000LL)) {
    throw invalid_argument("immediate value out of range");
  } else if ((value > 0x7F) || (value < -0x80)) {
    op = Operation::MATH_IMM32;
  } else {
    op = Operation::MATH_IMM8;
  }

  uint8_t z = (math_op >> 3) & 7;
  string ret = generate_rm(op, to, z, size);
  if ((op == Operation::MATH8_IMM8) || (op == Operation::MATH_IMM8)) {
    ret += static_cast<uint8_t>(value);
  } else if (op == Operation::MATH_IMM32) {
    ret.append(reinterpret_cast<const char*>(&value), 4);
  }
  return ret;
}



string generate_add(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::ADD_STORE8, to, from, size);
}

string generate_add(const MemoryReference& to, int64_t value,
    OperandSize size) {
  return generate_imm_math(Operation::ADD_STORE8, to, value, size);
}

string generate_or(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::OR_STORE8, to, from, size);
}

string generate_or(const MemoryReference& to, int64_t value,
    OperandSize size) {
  return generate_imm_math(Operation::OR_STORE8, to, value, size);
}

string generate_adc(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::ADC_STORE8, to, from, size);
}

string generate_adc(const MemoryReference& to, int64_t value,
    OperandSize size) {
  return generate_imm_math(Operation::ADC_STORE8, to, value, size);
}

string generate_sbb(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::SBB_STORE8, to, from, size);
}

string generate_sbb(const MemoryReference& to, int64_t value,
    OperandSize size) {
  return generate_imm_math(Operation::SBB_STORE8, to, value, size);
}

string generate_and(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::AND_STORE8, to, from, size);
}

string generate_and(const MemoryReference& to, int64_t value,
    OperandSize size) {
  return generate_imm_math(Operation::AND_STORE8, to, value, size);
}

string generate_sub(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::SUB_STORE8, to, from, size);
}

string generate_sub(const MemoryReference& to, int64_t value,
    OperandSize size) {
  return generate_imm_math(Operation::SUB_STORE8, to, value, size);
}

string generate_xor(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::XOR_STORE8, to, from, size);
}

string generate_xor(const MemoryReference& to, int64_t value,
    OperandSize size) {
  return generate_imm_math(Operation::XOR_STORE8, to, value, size);
}

string generate_cmp(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  return generate_load_store(Operation::CMP_STORE8, to, from, size);
}

string generate_cmp(const MemoryReference& to, int64_t value,
    OperandSize size) {
  return generate_imm_math(Operation::CMP_STORE8, to, value, size);
}

string generate_not(const MemoryReference& target, OperandSize size) {
  return generate_rm(Operation::NOT_NEG, target, 2, size);
}

string generate_neg(const MemoryReference& target, OperandSize size) {
  return generate_rm(Operation::NOT_NEG, target, 3, size);
}



string generate_test(const MemoryReference& a, const MemoryReference& b,
    OperandSize size) {
  if (a.field_size && b.field_size) {
    throw invalid_argument("test opcode can have at most one memory reference");
  }
  if (a.field_size) {
    return generate_rm(Operation::TEST, a, b.base_register, size);
  }
  return generate_rm(Operation::TEST, b, a.base_register, size);
}

string generate_seto(const MemoryReference& target) {
  return generate_rm(Operation::SETO, target, 0, OperandSize::Byte);
}

string generate_setno(const MemoryReference& target) {
  return generate_rm(Operation::SETNO, target, 0, OperandSize::Byte);
}

string generate_setb(const MemoryReference& target) {
  return generate_rm(Operation::SETB, target, 0, OperandSize::Byte);
}

string generate_setnae(const MemoryReference& target) {
  return generate_rm(Operation::SETNAE, target, 0, OperandSize::Byte);
}

string generate_setc(const MemoryReference& target) {
  return generate_rm(Operation::SETC, target, 0, OperandSize::Byte);
}

string generate_setnb(const MemoryReference& target) {
  return generate_rm(Operation::SETNB, target, 0, OperandSize::Byte);
}

string generate_setae(const MemoryReference& target) {
  return generate_rm(Operation::SETAE, target, 0, OperandSize::Byte);
}

string generate_setnc(const MemoryReference& target) {
  return generate_rm(Operation::SETNC, target, 0, OperandSize::Byte);
}

string generate_setz(const MemoryReference& target) {
  return generate_rm(Operation::SETZ, target, 0, OperandSize::Byte);
}

string generate_sete(const MemoryReference& target) {
  return generate_rm(Operation::SETE, target, 0, OperandSize::Byte);
}

string generate_setnz(const MemoryReference& target) {
  return generate_rm(Operation::SETNZ, target, 0, OperandSize::Byte);
}

string generate_setne(const MemoryReference& target) {
  return generate_rm(Operation::SETNE, target, 0, OperandSize::Byte);
}

string generate_setbe(const MemoryReference& target) {
  return generate_rm(Operation::SETBE, target, 0, OperandSize::Byte);
}

string generate_setna(const MemoryReference& target) {
  return generate_rm(Operation::SETNA, target, 0, OperandSize::Byte);
}

string generate_setnbe(const MemoryReference& target) {
  return generate_rm(Operation::SETNBE, target, 0, OperandSize::Byte);
}

string generate_seta(const MemoryReference& target) {
  return generate_rm(Operation::SETA, target, 0, OperandSize::Byte);
}

string generate_sets(const MemoryReference& target) {
  return generate_rm(Operation::SETS, target, 0, OperandSize::Byte);
}

string generate_setns(const MemoryReference& target) {
  return generate_rm(Operation::SETNS, target, 0, OperandSize::Byte);
}

string generate_setp(const MemoryReference& target) {
  return generate_rm(Operation::SETP, target, 0, OperandSize::Byte);
}

string generate_setpe(const MemoryReference& target) {
  return generate_rm(Operation::SETPE, target, 0, OperandSize::Byte);
}

string generate_setnp(const MemoryReference& target) {
  return generate_rm(Operation::SETNP, target, 0, OperandSize::Byte);
}

string generate_setpo(const MemoryReference& target) {
  return generate_rm(Operation::SETPO, target, 0, OperandSize::Byte);
}

string generate_setl(const MemoryReference& target) {
  return generate_rm(Operation::SETL, target, 0, OperandSize::Byte);
}

string generate_setnge(const MemoryReference& target) {
  return generate_rm(Operation::SETNGE, target, 0, OperandSize::Byte);
}

string generate_setnl(const MemoryReference& target) {
  return generate_rm(Operation::SETNL, target, 0, OperandSize::Byte);
}

string generate_setge(const MemoryReference& target) {
  return generate_rm(Operation::SETGE, target, 0, OperandSize::Byte);
}

string generate_setle(const MemoryReference& target) {
  return generate_rm(Operation::SETLE, target, 0, OperandSize::Byte);
}

string generate_setng(const MemoryReference& target) {
  return generate_rm(Operation::SETNG, target, 0, OperandSize::Byte);
}

string generate_setnle(const MemoryReference& target) {
  return generate_rm(Operation::SETNLE, target, 0, OperandSize::Byte);
}

string generate_setg(const MemoryReference& target) {
  return generate_rm(Operation::SETG, target, 0, OperandSize::Byte);
}


/*
mov encoding figuring

prefix = 01001DIB
D: non-memory reg is extended
I: index reg is extended
B: base reg is extended

mov rx, [ry] -- 48 8B 00XXXYYY
mov rx, [ry+ZZ] -- 48 8B 01XXXYYY ZZ
mov rx, [ry+ZZZZZZZZ] -- 48 8B 10XXXYYY ZZ ZZ ZZ ZZ
mov rx, [ry+m*rw+ZZ] -- 48 89 01XXX100 MMWWWYYY ZZ
mov rx, [ry+m*rw+ZZZZZZZZ] -- 48 89 10XXX100 MMWWWYYY ZZ ZZ ZZ ZZ
M: 0 = *1, 1 = *2, 2 = *4, 3 = *8
0:  48 89 44 37 24          mov    QWORD PTR [rdi+rsi*1+0x24],rax
5:  48 89 44 77 24          mov    QWORD PTR [rdi+rsi*2+0x24],rax
a:  48 89 44 b7 24          mov    QWORD PTR [rdi+rsi*4+0x24],rax
f:  48 89 44 f7 24          mov    QWORD PTR [rdi+rsi*8+0x24],rax
14: 48 89 44 ef 24          mov    QWORD PTR [rdi+rbp*8+0x24],rax
19: 48 89 4c f7 24          mov    QWORD PTR [rdi+rsi*8+0x24],rcx
1e: 48 89 44 f6 24          mov    QWORD PTR [rsi+rsi*8+0x24],rax
23: 48 89 84 f6 24 24 00 00 mov    QWORD PTR [rsi+rsi*8+0x2424],rax
2b: 48 8b 44 f6 24          mov    rax,QWORD PTR [rsi+rsi*8+0x24]

0:  48 89 44 3f 24          mov    QWORD PTR [rdi+rdi*1+0x24],rax
a:  49 89 44 3f 24          mov    QWORD PTR [r15+rdi*1+0x24],rax
5:  4a 89 44 3f 24          mov    QWORD PTR [rdi+r15*1+0x24],rax
f:  4b 89 44 3f 24          mov    QWORD PTR [r15+r15*1+0x24],rax
14: 4c 89 44 3f 24          mov    QWORD PTR [rdi+rdi*1+0x24],r8
1e: 4d 89 44 3f 24          mov    QWORD PTR [r15+rdi*1+0x24],r8
19: 4e 89 44 3f 24          mov    QWORD PTR [rdi+r15*1+0x24],r8
23: 4f 89 44 3f 24          mov    QWORD PTR [r15+r15*1+0x24],r8


mov [ry], rx -- 48 89 00XXXYYY
mov rx, [ry] -- 48 8B 00XXXYYY
mov [ey], ex -- 67 89 00XXXYYY
mov ex, [ey] -- 67 8B 00XXXYYY
mov [y], x   -- 89 00XXXYYY
mov x, [y]   -- 8B 00XXXYYY
mov [yl], xl -- 88 00XXXYYY
mov xl, [yl] -- 8A 00XXXYYY

mov [ry+ZZ], rx -- 48 89 01XXXYYY ZZ
mov rx, [ry+ZZ] -- 48 8B 01XXXYYY ZZ
mov [ey+ZZ], ex -- 67 89 01XXXYYY ZZ
mov ex, [ey+ZZ] -- 67 8B 01XXXYYY ZZ
mov [y+ZZ], x   -- 89 01XXXYYY ZZ
mov x, [y+ZZ]   -- 8B 01XXXYYY ZZ
mov [yl+ZZ], xl -- 88 01XXXYYY ZZ
mov xl, [yl+ZZ] -- 8A 01XXXYYY ZZ

mov [ry+ZZZZZZZZ], rx -- 48 89 10XXXYYY ZZ ZZ ZZ ZZ
mov rx, [ry+ZZZZZZZZ] -- 48 8B 10XXXYYY ZZ ZZ ZZ ZZ
mov [ey+ZZZZZZZZ], ex -- 67 89 10XXXYYY ZZ ZZ ZZ ZZ
mov ex, [ey+ZZZZZZZZ] -- 67 8B 10XXXYYY ZZ ZZ ZZ ZZ
mov [y+ZZZZZZZZ], x   -- 89 10XXXYYY ZZ ZZ ZZ ZZ
mov x, [y+ZZZZZZZZ]   -- 8B 10XXXYYY ZZ ZZ ZZ ZZ
mov [yl+ZZZZZZZZ], xl -- 88 10XXXYYY ZZ ZZ ZZ ZZ
mov xl, [yl+ZZZZZZZZ] -- 8A 10XXXYYY ZZ ZZ ZZ ZZ

40     REX          Access to new 8-bit registers
41     REX.B        Extension of r/m field, base field, or opcode reg field
42     REX.X        Extension of SIB index field
43     REX.XB       REX.X and REX.B combination
44     REX.R        Extension of ModR/M reg field
45     REX.RB       REX.R and REX.B combination
46     REX.RX       REX.R and REX.X combination
47     REX.RXB      REX.R, REX.X and REX.B combination
48     REX.W        64 Bit Operand Size
49     REX.WB       REX.W and REX.B combination
4A     REX.WX       REX.W and REX.X combination
4B     REX.WXB      REX.W, REX.X and REX.B combination
4C     REX.WR       REX.W and REX.R combination
4D     REX.WRB      REX.W, REX.R and REX.B combination
4E     REX.WRX      REX.W, REX.R and REX.X combination
4F     REX.WRXB     REX.W, REX.R, REX.X and REX.B combination






r8(/r)                       AL   CL   DL   BL   AH   CH   DH   BH
r16(/r)                      AX   CX   DX   BX   SP   BP   SI   DI
r32(/r)                      EAX  ECX  EDX  EBX  ESP  EBP  ESI  EDI
mm(/r)                       MM0  MM1  MM2  MM3  MM4  MM5  MM6  MM7
xmm(/r)                      XMM0 XMM1 XMM2 XMM3 XMM4 XMM5 XMM6 XMM7
(In decimal) /digit (Opcode) 0    1    2    3    4    5    6    7
(In binary) REG =            000  001  010  011  100  101  110  111
Effective Address Mod R/M    Value of ModR/M Byte (in Hexadecimal)
[EAX]              00 000    00   08   10   18   20   28   30   38
[ECX]                 001    01   09   11   19   21   29   31   39
[EDX]                 010    02   0A   12   1A   22   2A   32   3A
[EBX]                 011    03   0B   13   1B   23   2B   33   3B
[--][--] *1           100    04   0C   14   1C   24   2C   34   3C
disp32 *2             101    05   0D   15   1D   25   2D   35   3D
[ESI]                 110    06   0E   16   1E   26   2E   36   3E
[EDI]                 111    07   0F   17   1F   27   2F   37   3F
[EAX]+disp8 *3     01 000    40   48   50   58   60   68   70   78
[ECX]+disp8           001    41   49   51   59   61   69   71   79
[EDX]+disp8           010    42   4A   52   5A   62   6A   72   7A
[EBX]+disp8           011    43   4B   53   5B   63   6B   73   7B
[--][--]+disp8        100    44   4C   54   5C   64   6C   74   7C
[EBP]+disp8           101    45   4D   55   5D   65   6D   75   7D
[ESI]+disp8           110    46   4E   56   5E   66   6E   76   7E
[EDI]+disp8           111    47   4F   57   5F   67   6F   77   7F
[EAX]+disp32       10 000    80   88   90   98   A0   A8   B0   B8
[ECX]+disp32          001    81   89   91   99   A1   A9   B1   B9
[EDX]+disp32          010    82   8A   92   9A   A2   AA   B2   BA
[EBX]+disp32          011    83   8B   93   9B   A3   AB   B3   BB
[--][--]+disp32       100    84   8C   94   9C   A4   AC   B4   BC
[EBP]+disp32          101    85   8D   95   9D   A5   AD   B5   BD
[ESI]+disp32          110    86   8E   96   9E   A6   AE   B6   BE
[EDI]+disp32          111    87   8F   97   9F   A7   AF   B7   BF
EAX/AX/AL/MM0/XMM0 11 000    C0   C8   D0   D8   E0   E8   F0   F8
ECX/CX/CL/MM/XMM1     001    C1   C9   D1   D9   E1   E9   F1   F9
EDX/DX/DL/MM2/XMM2    010    C2   CA   D2   DA   E2   EA   F2   FA
EBX/BX/BL/MM3/XMM3    011    C3   CB   D3   DB   E3   EB   F3   FB
ESP/SP/AH/MM4/XMM4    100    C4   CC   D4   DC   E4   EC   F4   FC
EBP/BP/CH/MM5/XMM5    101    C5   CD   D5   DD   E5   ED   F5   FD
ESI/SI/DH/MM6/XMM6    110    C6   CE   D6   DE   E6   EE   F6   FE
EDI/DI/BH/MM7/XMM7    111    C7   CF   D7   DF   E7   EF   F7   FF
NOTES:
1. The [--][--] nomenclature means a SIB follows the ModR/M byte.
2. The disp32 nomenclature denotes a 32-bit displacement that follows the ModR/M byte (or the SIB byte if one is present) and that is added to the index.
3. The disp8 nomenclature denotes an 8-bit displacement that follows the ModR/M byte (or the SIB byte if one is present) and that is sign-extended and added to the index.

100010DY BBSSSZZZ
D = direction (0 = reg to mem, 1 = mem to reg)
Y = size (0 = 8-bit, 1 = 16/32/64-bit)
B = behavior (0 = memory access by register contents, 1 = memory by register contents + disp8, 2 = memory by register contents + disp32, 3 = register value)
S = non-memory-reference register
Z = memory reference base register
      special case: if B != 3 and Z = 4, then use SIB byte (indexing)
      special case: if B = 0 and Z = 5, don't use a register at all, just read from memory at disp32

don't support indexing for now

mov = 88-8B
add = 00-03
or = 08-0B
adc = 10-13
sbb = 18-1B
and = 20-23
sub = 28-2B
xor = 30-33
cmp = 38-3B
imul = 69,6B (no 8-bit analogs)

jumps
70   JO             rel8   o.......   Jump short if overflow (OF=1)
71   JNO            rel8   o.......   Jump short if not overflow (OF=0)
72   JB/JNAE/JC     rel8   .......c   Jump short if below/not above or equal/carry (CF=1)
73   JNB/JAE/JNC    rel8   .......c   Jump short if not below/above or equal/not carry (CF=0)
74   JZ/JE          rel8   ....z...   Jump short if zero/equal (ZF=1)
75   JNZ/JNE        rel8   ....z...   Jump short if not zero/not equal (ZF=0)
76   JBE/JNA        rel8   ....z..c   Jump short if below or equal/not above (CF=1 OR ZF=1)
77   JNBE/JA        rel8   ....z..c   Jump short if not below or equal/above (CF=0 AND ZF=0)
78   JS             rel8   ...s....   Jump short if sign (SF=1)
79   JNS            rel8   ...s....   Jump short if not sign (SF=0)
7A   JP/JPE         rel8   ......p.   Jump short if parity/parity even (PF=1)
7B   JNP/JPO        rel8   ......p.   Jump short if not parity/parity odd (PF=0)
7C   JL/JNGE        rel8   o..s....   Jump short if less/not greater (SF!=OF)
7D   JNL/JGE        rel8   o..s....   Jump short if not less/greater or equal (SF=OF)
7E   JLE/JNG        rel8   o..sz...   Jump short if less or equal/not greater ((ZF=1) OR (SF!=OF))
7F   JNLE/JG        rel8   o..sz...   Jump short if not less nor equal/greater ((ZF=0) AND (SF=OF))

80/81/83 = arith/bitwise/cmp with immediate
84/85 = test
86/87 = xchg

88-8B = mov (see above)
9C = pushf (flags)
9D = popf (flags)
C0 = rotate/shift
C2 = ret 16
C3 = ret
C6/C7 = mov mem, imm
D0,D1 = rotate/shift by 1
D2,D3 = rotate/shift by CL
D8-DF = floating-point opcodes
E8 = call
E9 = jump32
EB = jump8
F0 = lock

*/
