#include "AMD64Assembler.hh"

#include <string>

using namespace std;


const char* name_for_register(Register r, OperandSize size) {
  switch (size) {
    case OperandSize::Byte:
      switch (r) {
        case Register::None:
          return "None";
        case Register::AL:
          return "AL";
        case Register::CL:
          return "CL";
        case Register::DL:
          return "DL";
        case Register::BL:
          return "BL";
        case Register::AH:
          return "AH";
        case Register::CH:
          return "CH";
        case Register::DH:
          return "DH";
        case Register::BH:
          return "BH";
        case Register::R8B:
          return "R8B";
        case Register::R9B:
          return "R9B";
        case Register::R10B:
          return "R10B";
        case Register::R11B:
          return "R11B";
        case Register::R12B:
          return "R12B";
        case Register::R13B:
          return "R13B";
        case Register::R14B:
          return "R14B";
        case Register::R15B:
          return "R15B";
        default:
          return "UNKNOWN8";
      }
    case OperandSize::Word:
      switch (r) {
        case Register::None:
          return "None";
        case Register::AX:
          return "AX";
        case Register::CX:
          return "CX";
        case Register::DX:
          return "DX";
        case Register::BX:
          return "BX";
        case Register::SP:
          return "SP";
        case Register::BP:
          return "BP";
        case Register::SI:
          return "SI";
        case Register::DI:
          return "DI";
        case Register::R8W:
          return "R8W";
        case Register::R9W:
          return "R9W";
        case Register::R10W:
          return "R10W";
        case Register::R11W:
          return "R11W";
        case Register::R12W:
          return "R12W";
        case Register::R13W:
          return "R13W";
        case Register::R14W:
          return "R14W";
        case Register::R15W:
          return "R15W";
        default:
          return "UNKNOWN16";
      }
    case OperandSize::DoubleWord:
      switch (r) {
        case Register::None:
          return "None";
        case Register::EAX:
          return "EAX";
        case Register::ECX:
          return "ECX";
        case Register::EDX:
          return "EDX";
        case Register::EBX:
          return "EBX";
        case Register::ESP:
          return "ESP";
        case Register::EBP:
          return "EBP";
        case Register::ESI:
          return "ESI";
        case Register::EDI:
          return "EDI";
        case Register::R8D:
          return "R8D";
        case Register::R9D:
          return "R9D";
        case Register::R10D:
          return "R10D";
        case Register::R11D:
          return "R11D";
        case Register::R12D:
          return "R12D";
        case Register::R13D:
          return "R13D";
        case Register::R14D:
          return "R14D";
        case Register::R15D:
          return "R15D";
        default:
          return "UNKNOWN32";
      }
    case OperandSize::QuadWord:
      switch (r) {
        case Register::None:
          return "None";
        case Register::RAX:
          return "RAX";
        case Register::RCX:
          return "RCX";
        case Register::RDX:
          return "RDX";
        case Register::RBX:
          return "RBX";
        case Register::RSP:
          return "RSP";
        case Register::RBP:
          return "RBP";
        case Register::RSI:
          return "RSI";
        case Register::RDI:
          return "RDI";
        case Register::R8:
          return "R8";
        case Register::R9:
          return "R9";
        case Register::R10:
          return "R10";
        case Register::R11:
          return "R11";
        case Register::R12:
          return "R12";
        case Register::R13:
          return "R13";
        case Register::R14:
          return "R14";
        case Register::R15:
          return "R15";
        default:
          return "UNKNOWN64";
      }
  }
  return "UNKNOWN";
}


MemoryReference::MemoryReference(Register base_register, int64_t offset,
    Register index_register, uint8_t field_size) : base_register(base_register),
    index_register(index_register), field_size(field_size), offset(offset) { }
MemoryReference::MemoryReference(Register base_register) :
    base_register(base_register), index_register(Register::None), field_size(0),
    offset(0) { }

static inline bool is_extension_register(Register r) {
  return static_cast<int8_t>(r) >= 8;
}



void AMD64Assembler::write_label(const std::string& name) {
  this->labels.emplace_back(name, this->stream.size());
  if (!this->name_to_label.emplace(name, &this->labels.back()).second) {
    throw invalid_argument("duplicate label name: " + name);
  }
}



string AMD64Assembler::generate_rm(Operation op, const MemoryReference& mem,
    Register reg, OperandSize size) {
  uint16_t opcode = static_cast<uint16_t>(op);

  string ret;
  if (!mem.field_size) { // behavior = 3 (register reference)
    bool mem_ext = is_extension_register(mem.base_register);
    bool reg_ext = is_extension_register(reg);

    uint8_t prefix_byte = 0x40 | (mem_ext ? 0x01 : 0) | (reg_ext ? 0x04 : 0);
    if (size == OperandSize::QuadWord) {
      prefix_byte |= 0x08;
    } else if (size == OperandSize::Word) {
      ret += 0x66;
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

  // TODO: implement these cases
  if (mem.base_register == Register::None) {
    throw invalid_argument("memory references without base not supported");
  }

  bool reg_ext = is_extension_register(reg);
  bool mem_index_ext = is_extension_register(mem.index_register);
  bool mem_base_ext = is_extension_register(mem.base_register);

  uint8_t rm_byte = ((reg & 7) << 3);
  uint8_t sib_byte = 0;
  if (mem.field_size) {
    rm_byte |= 0x04;

    if (mem.field_size == 8) {
      sib_byte = 0xC0;
    } else if (mem.field_size == 4) {
      sib_byte = 0x80;
    } else if (mem.field_size == 2) {
      sib_byte = 0x40;
    } else if (mem.field_size != 1) {
      throw invalid_argument("field size must be 1, 2, 4, or 8");
    }

    if (mem.base_register == Register::RBP) {
      throw invalid_argument("RBP cannot be used as a base register in index addressing");
    }
    if (mem.index_register == Register::RSP) {
      throw invalid_argument("RSP cannot be used as a base register in index addressing");
    }

    sib_byte |= mem.base_register & 7;
    if (mem.index_register == Register::None) {
      sib_byte |= ((Register::RSP & 7) << 3);
    } else {
      sib_byte |= ((mem.index_register & 7) << 3);
    }

    if (mem.base_register == Register::RIP) {
      throw invalid_argument("RIP cannot be used with scaled index addressing");
    }

  } else {
    if (mem.base_register == Register::RIP) {
      rm_byte |= 0x05;
    } else {
      rm_byte |= (mem.base_register & 7);
    }
  }

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
  // RBP or R13, then add a fake offset of 0 to the opcode (this is because this
  // case is shadowed by the RIP special case above)
  if ((mem.offset == 0) && ((rm_byte & 7) != 0x04) &&
      ((mem.base_register == Register::RSP) || (mem.base_register == Register::R13))) {
    rm_byte |= 0x40;
  }

  // fill in the ret string
  uint8_t prefix_byte = 0x40 | (reg_ext ? 0x04 : 0) | (mem_index_ext ? 0x02 : 0) |
      (mem_base_ext ? 0x01 : 0);
  if (size == OperandSize::QuadWord) {
    prefix_byte |= 0x08;
  } else if (size == OperandSize::Word) {
    ret += 0x66;
  }

  if (prefix_byte != 0x40) {
    ret += prefix_byte;
  }

  if (opcode > 0xFF) {
    ret += (opcode >> 8);
  }
  ret += (opcode & 0xFF);

  ret += rm_byte;
  if ((rm_byte & 0x07) == 0x04) {
    ret += sib_byte;
  }
  if (rm_byte & 0x40) {
    ret.append(reinterpret_cast<const char*>(&mem.offset), 1);
  } else if ((rm_byte & 0x80) || (mem.base_register == Register::RIP)) {
    ret.append(reinterpret_cast<const char*>(&mem.offset), 4);
  }
  return ret;
}

string AMD64Assembler::generate_rm(Operation op, const MemoryReference& mem,
    uint8_t z, OperandSize size) {
  return AMD64Assembler::generate_rm(op, mem, static_cast<Register>(z), size);
}

void AMD64Assembler::write_rm(Operation op, const MemoryReference& mem,
    Register reg, OperandSize size) {
  this->write(this->generate_rm(op, mem, reg, size));
}

void AMD64Assembler::write_rm(Operation op, const MemoryReference& mem,
    uint8_t z, OperandSize size) {
  this->write(this->generate_rm(op, mem, z, size));
}




Operation AMD64Assembler::load_store_oper_for_args(Operation op,
    const MemoryReference& to, const MemoryReference& from, OperandSize size) {
  return static_cast<Operation>(op | ((size != OperandSize::Byte) ? 1 : 0) |
      (from.field_size ? 2 : 0));
}

void AMD64Assembler::write_load_store(Operation base_op, const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  if (to.field_size && from.field_size) {
    throw invalid_argument("load/store opcodes can have at most one memory reference");
  }

  Operation op = load_store_oper_for_args(base_op, to, from, size);
  if (!from.field_size) {
    this->write_rm(op, to, from.base_register, size);
  } else {
    this->write_rm(op, from, to.base_register, size);
  }
}



void AMD64Assembler::write_push(Register r) {
  string data;
  if (is_extension_register(r)) {
    data += 0x41;
    data += (static_cast<uint8_t>(r) - 8) | 0x50;
  } else {
    data += r | 0x50;
  }
  this->write(data);
}

void AMD64Assembler::write_push(int64_t value) {
  string data;
  if ((value >= -0x80) && (value <= 0x7F)) {
    data += Operation::PUSH8;
    data += static_cast<int8_t>(value);
  } else if ((value >= -0x80000000LL) && (value <= 0x7FFFFFFFLL)) {
    data += Operation::PUSH32;
    data.append(reinterpret_cast<const char*>(&value), 4);
  } else {
    // emulate it with a push32 + mov32
    data += Operation::PUSH32;
    data.append(reinterpret_cast<const char*>(&value), 4);
    data += 0xC7;
    data += 0x44;
    data += 0x24;
    data += 0x04;
    data.append(reinterpret_cast<const char*>(&value) + 4, 4);
  }
  this->write(data);
}

void AMD64Assembler::write_push(const MemoryReference& mem) {
  this->write_rm(Operation::PUSH_RM, mem, 6, OperandSize::DoubleWord);
}

void AMD64Assembler::write_pop(Register r) {
  string data;
  if (is_extension_register(r)) {
    data += 0x41;
    data += (static_cast<uint8_t>(r) - 8) | 0x58;
  } else {
    data += r | 0x58;
  }
  this->write(data);
}



void AMD64Assembler::write_mov(const MemoryReference& to, const MemoryReference& from,
    OperandSize size) {
  this->write_load_store(Operation::MOV_STORE8, to, from, size);
}

void AMD64Assembler::write_mov(Register reg, int64_t value, OperandSize size) {
  if (value == 0) {
    // xor reg, reg
    MemoryReference r(reg);
    this->write_xor(r, r, size);
    return;
  }

  string data;
  if (size == OperandSize::QuadWord) {
    // TODO: we can optimize for code size by not using movabs for small values,
    // but for now I'm lazy
    data += 0x48 | (is_extension_register(reg) ? 0x01 : 0);
    data += 0xB8 | (reg & 7);
    data.append(reinterpret_cast<const char*>(&value), 8);

  } else if (size == OperandSize::DoubleWord) {
    string data;
    if (is_extension_register(reg)) {
      data += 0x41;
    }
    data += 0xB8 | (reg & 7);
    data.append(reinterpret_cast<const char*>(&value), 4);

  } else if (size == OperandSize::Word) {
    string data;
    data += 0x66;
    if (is_extension_register(reg)) {
      data += 0x41;
    }
    data += 0xB8 | (reg & 7);
    data.append(reinterpret_cast<const char*>(&value), 2);

  } else if (size == OperandSize::Byte) {
    string data;
    if (is_extension_register(reg)) {
      data += 0x41;
    }
    data += 0xB0 | (reg & 7);
    data += static_cast<int8_t>(value);

  } else {
    throw invalid_argument("unknown operand size");
  }
  this->write(data);
}

void AMD64Assembler::write_xchg(Register reg, const MemoryReference& mem,
    OperandSize size) {
  Operation op = (size == OperandSize::Byte) ? Operation::XCHG8 : Operation::XCHG;
  this->write_rm(op, mem, reg, size);
}



void AMD64Assembler::write_nop() {
  static string nop("\x90", 1);
  this->write(nop);
}

void AMD64Assembler::write_jmp(const std::string& label_name) {
  this->stream.emplace_back(label_name, Operation::JMP8, Operation::JMP32);
}

void AMD64Assembler::write_jmp(const MemoryReference& mem) {
  this->write_rm(Operation::CALL_JMP_ABS, mem, 4, OperandSize::DoubleWord);
}

std::string AMD64Assembler::generate_jmp(Operation op8, Operation op32,
    int64_t opcode_address, int64_t target_address) {
  int64_t offset = target_address - opcode_address;

  if (op8) { // may be omitted for call opcodes
    int64_t offset8 = offset - 2 - (static_cast<int64_t>(op8) > 0xFF);
    if ((offset8 >= -0x80) && (offset8 <= 0x7F)) {
      string data;
      if (op8 > 0xFF) {
        data += (op8 >> 8) & 0xFF;
      }
      data += op8 & 0xFF;
      data.append(reinterpret_cast<const char*>(&offset8), 1);
      return data;
    }
  }

  int64_t offset32 = offset - 5 - (static_cast<int64_t>(op32) > 0xFF);
  if ((offset32 >= -0x80000000LL) && (offset32 <= 0x7FFFFFFFLL)) {
    string data;
    if (op32 > 0xFF) {
      data += (op32 >> 8) & 0xFF;
    }
    data += op32 & 0xFF;
    data.append(reinterpret_cast<const char*>(&offset32), 4);
    return data;
  }

  // the nasty case: we have to use a 64-bit offset. here we do this by putting
  // the address on the stack, and "returning" to it
  // TODO: support conditional jumps and 64-bit calls here
  if (op32 != Operation::JMP32) {
    throw runtime_error("64-bit calls and conditional jumps not yet implemented");
  }
  string data;
  // push <low 4 bytes of address>
  data += 0x68;
  data.append(reinterpret_cast<const char*>(&target_address), 4);
  // mov [rsp+4], <high 4 bytes of address>
  data += 0xC7;
  data += 0x44;
  data += 0x24;
  data += 0x04;
  data.append(reinterpret_cast<const char*>(&target_address) + 4, 4);
  // ret
  data += 0xC3;
  return data;
}

void AMD64Assembler::write_call(const std::string& label_name) {
  this->stream.emplace_back(label_name, Operation::ADD_STORE8, Operation::CALL32);
}

void AMD64Assembler::write_call(const MemoryReference& mem) {
  this->write_rm(Operation::CALL_JMP_ABS, mem, 2, OperandSize::DoubleWord);
}

void AMD64Assembler::write_call(int64_t offset) {
  string data;
  data += Operation::CALL32;
  data.append(reinterpret_cast<const char*>(&offset), 4);
  this->write(data);
}

void AMD64Assembler::write_ret(uint16_t stack_bytes) {
  if (stack_bytes) {
    string data("\xC2", 1);
    data.append(reinterpret_cast<const char*>(&stack_bytes), 2);
    this->write(data);
  } else {
    this->write("\xC3");
  }
}



void AMD64Assembler::write_jcc(Operation op8, Operation op,
    const std::string& label_name) {
  this->stream.emplace_back(label_name, op8, op);
}

void AMD64Assembler::write_jo(const string& label_name) {
  this->write_jcc(Operation::JO8, Operation::JO, label_name);
}

void AMD64Assembler::write_jno(const string& label_name) {
  this->write_jcc(Operation::JNO8, Operation::JNO, label_name);
}

void AMD64Assembler::write_jb(const string& label_name) {
  this->write_jcc(Operation::JB8, Operation::JB, label_name);
}

void AMD64Assembler::write_jnae(const string& label_name) {
  this->write_jcc(Operation::JNAE8, Operation::JNAE, label_name);
}

void AMD64Assembler::write_jc(const string& label_name) {
  this->write_jcc(Operation::JC8, Operation::JC, label_name);
}

void AMD64Assembler::write_jnb(const string& label_name) {
  this->write_jcc(Operation::JNB8, Operation::JNB, label_name);
}

void AMD64Assembler::write_jae(const string& label_name) {
  this->write_jcc(Operation::JAE8, Operation::JAE, label_name);
}

void AMD64Assembler::write_jnc(const string& label_name) {
  this->write_jcc(Operation::JNC8, Operation::JNC, label_name);
}

void AMD64Assembler::write_jz(const string& label_name) {
  this->write_jcc(Operation::JZ8, Operation::JZ, label_name);
}

void AMD64Assembler::write_je(const string& label_name) {
  this->write_jcc(Operation::JE8, Operation::JE, label_name);
}

void AMD64Assembler::write_jnz(const string& label_name) {
  this->write_jcc(Operation::JNZ8, Operation::JNZ, label_name);
}

void AMD64Assembler::write_jne(const string& label_name) {
  this->write_jcc(Operation::JNE8, Operation::JNE, label_name);
}

void AMD64Assembler::write_jbe(const string& label_name) {
  this->write_jcc(Operation::JBE8, Operation::JBE, label_name);
}

void AMD64Assembler::write_jna(const string& label_name) {
  this->write_jcc(Operation::JNA8, Operation::JNA, label_name);
}

void AMD64Assembler::write_jnbe(const string& label_name) {
  this->write_jcc(Operation::JNBE8, Operation::JNBE, label_name);
}

void AMD64Assembler::write_ja(const string& label_name) {
  this->write_jcc(Operation::JA8, Operation::JA, label_name);
}

void AMD64Assembler::write_js(const string& label_name) {
  this->write_jcc(Operation::JS8, Operation::JS, label_name);
}

void AMD64Assembler::write_jns(const string& label_name) {
  this->write_jcc(Operation::JNS8, Operation::JNS, label_name);
}

void AMD64Assembler::write_jp(const string& label_name) {
  this->write_jcc(Operation::JP8, Operation::JP, label_name);
}

void AMD64Assembler::write_jpe(const string& label_name) {
  this->write_jcc(Operation::JPE8, Operation::JPE, label_name);
}

void AMD64Assembler::write_jnp(const string& label_name) {
  this->write_jcc(Operation::JNP8, Operation::JNP, label_name);
}

void AMD64Assembler::write_jpo(const string& label_name) {
  this->write_jcc(Operation::JPO8, Operation::JPO, label_name);
}

void AMD64Assembler::write_jl(const string& label_name) {
  this->write_jcc(Operation::JL8, Operation::JL, label_name);
}

void AMD64Assembler::write_jnge(const string& label_name) {
  this->write_jcc(Operation::JNGE8, Operation::JNGE, label_name);
}

void AMD64Assembler::write_jnl(const string& label_name) {
  this->write_jcc(Operation::JNL8, Operation::JNL, label_name);
}

void AMD64Assembler::write_jge(const string& label_name) {
  this->write_jcc(Operation::JGE8, Operation::JGE, label_name);
}

void AMD64Assembler::write_jle(const string& label_name) {
  this->write_jcc(Operation::JLE8, Operation::JLE, label_name);
}

void AMD64Assembler::write_jng(const string& label_name) {
  this->write_jcc(Operation::JNG8, Operation::JNG, label_name);
}

void AMD64Assembler::write_jnle(const string& label_name) {
  this->write_jcc(Operation::JNLE8, Operation::JNLE, label_name);
}

void AMD64Assembler::write_jg(const string& label_name) {
  this->write_jcc(Operation::JG8, Operation::JG, label_name);
}



void AMD64Assembler::write_imm_math(Operation math_op,
    const MemoryReference& to, int64_t value, OperandSize size) {
  if (math_op & 0xC7) {
    throw invalid_argument("immediate math opcodes must use basic Operation types");
  }

  Operation op;
  if (size == OperandSize::Byte) {
    op = Operation::MATH8_IMM8;
  } else if ((value < -0x80000000LL) || (value > 0x7FFFFFFFLL)) {
    throw invalid_argument("immediate value out of range");
  } else if ((value > 0x7F) || (value < -0x80)) {
    op = Operation::MATH_IMM32;
  } else {
    op = Operation::MATH_IMM8;
  }

  uint8_t z = (math_op >> 3) & 7;
  string data = this->generate_rm(op, to, z, size);
  if ((op == Operation::MATH8_IMM8) || (op == Operation::MATH_IMM8)) {
    data += static_cast<uint8_t>(value);
  } else if (op == Operation::MATH_IMM32) {
    data.append(reinterpret_cast<const char*>(&value), 4);
  }
  this->write(data);
}



void AMD64Assembler::write_add(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::ADD_STORE8, to, from, size);
}

void AMD64Assembler::write_add(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::ADD_STORE8, to, value, size);
}

void AMD64Assembler::write_or(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::OR_STORE8, to, from, size);
}

void AMD64Assembler::write_or(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::OR_STORE8, to, value, size);
}

void AMD64Assembler::write_adc(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::ADC_STORE8, to, from, size);
}

void AMD64Assembler::write_adc(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::ADC_STORE8, to, value, size);
}

void AMD64Assembler::write_sbb(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::SBB_STORE8, to, from, size);
}

void AMD64Assembler::write_sbb(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::SBB_STORE8, to, value, size);
}

void AMD64Assembler::write_and(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::AND_STORE8, to, from, size);
}

void AMD64Assembler::write_and(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::AND_STORE8, to, value, size);
}

void AMD64Assembler::write_sub(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::SUB_STORE8, to, from, size);
}

void AMD64Assembler::write_sub(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::SUB_STORE8, to, value, size);
}

void AMD64Assembler::write_xor(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::XOR_STORE8, to, from, size);
}

void AMD64Assembler::write_xor(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::XOR_STORE8, to, value, size);
}

void AMD64Assembler::write_cmp(const MemoryReference& to,
    const MemoryReference& from, OperandSize size) {
  this->write_load_store(Operation::CMP_STORE8, to, from, size);
}

void AMD64Assembler::write_cmp(const MemoryReference& to, int64_t value,
    OperandSize size) {
  this->write_imm_math(Operation::CMP_STORE8, to, value, size);
}

void AMD64Assembler::write_shift(uint8_t which, const MemoryReference& mem,
    uint8_t bits, OperandSize size) {
  if (bits == 1) {
    Operation op = (size == OperandSize::Byte) ? Operation::SHIFT8_1 : Operation::SHIFT_1;
    this->write_rm(op, mem, which, size);
  } else if (bits != 0xFF) {
    Operation op = (size == OperandSize::Byte) ? Operation::SHIFT8_IMM : Operation::SHIFT_IMM;
    string data = this->generate_rm(op, mem, which, size);
    data += bits;
    this->write(data);
  } else {
    Operation op = (size == OperandSize::Byte) ? Operation::SHIFT8_CL : Operation::SHIFT_CL;
    this->write_rm(op, mem, which, size);
  }
}

void AMD64Assembler::write_rol(const MemoryReference& mem, uint8_t bits, OperandSize size) {
  this->write_shift(0, mem, bits, size);
}

void AMD64Assembler::write_ror(const MemoryReference& mem, uint8_t bits, OperandSize size) {
  this->write_shift(1, mem, bits, size);
}

void AMD64Assembler::write_rcl(const MemoryReference& mem, uint8_t bits, OperandSize size) {
  this->write_shift(2, mem, bits, size);
}

void AMD64Assembler::write_rcr(const MemoryReference& mem, uint8_t bits, OperandSize size) {
  this->write_shift(3, mem, bits, size);
}

void AMD64Assembler::write_shl(const MemoryReference& mem, uint8_t bits, OperandSize size) {
  this->write_shift(4, mem, bits, size);
}

void AMD64Assembler::write_shr(const MemoryReference& mem, uint8_t bits, OperandSize size) {
  this->write_shift(5, mem, bits, size);
}

void AMD64Assembler::write_sar(const MemoryReference& mem, uint8_t bits, OperandSize size) {
  this->write_shift(7, mem, bits, size);
}

void AMD64Assembler::write_rol_cl(const MemoryReference& mem, OperandSize size) {
  this->write_shift(0, mem, 0xFF, size);
}

void AMD64Assembler::write_ror_cl(const MemoryReference& mem, OperandSize size) {
  this->write_shift(1, mem, 0xFF, size);
}

void AMD64Assembler::write_rcl_cl(const MemoryReference& mem, OperandSize size) {
  this->write_shift(2, mem, 0xFF, size);
}

void AMD64Assembler::write_rcr_cl(const MemoryReference& mem, OperandSize size) {
  this->write_shift(3, mem, 0xFF, size);
}

void AMD64Assembler::write_shl_cl(const MemoryReference& mem, OperandSize size) {
  this->write_shift(4, mem, 0xFF, size);
}

void AMD64Assembler::write_shr_cl(const MemoryReference& mem, OperandSize size) {
  this->write_shift(5, mem, 0xFF, size);
}

void AMD64Assembler::write_sar_cl(const MemoryReference& mem, OperandSize size) {
  this->write_shift(7, mem, 0xFF, size);
}



void AMD64Assembler::write_not(const MemoryReference& target,
    OperandSize size) {
  this->write_rm(Operation::NOT_NEG, target, 2, size);
}

void AMD64Assembler::write_neg(const MemoryReference& target,
    OperandSize size) {
  this->write_rm(Operation::NOT_NEG, target, 3, size);
}

void AMD64Assembler::write_inc(const MemoryReference& target,
    OperandSize size) {
  Operation op = (size == OperandSize::Byte) ? Operation::INC_DEC8 : Operation::INC_DEC;
  this->write_rm(op, target, 0, size);
}

void AMD64Assembler::write_dec(const MemoryReference& target,
    OperandSize size) {
  Operation op = (size == OperandSize::Byte) ? Operation::INC_DEC8 : Operation::INC_DEC;
  this->write_rm(op, target, 1, size);
}



void AMD64Assembler::write_test(const MemoryReference& a,
    const MemoryReference& b, OperandSize size) {
  if (a.field_size && b.field_size) {
    throw invalid_argument("test opcode can have at most one memory reference");
  }
  if (a.field_size) {
    this->write_rm(Operation::TEST, a, b.base_register, size);
  } else {
    this->write_rm(Operation::TEST, b, a.base_register, size);
  }
}

void AMD64Assembler::write_seto(const MemoryReference& target) {
  this->write_rm(Operation::SETO, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setno(const MemoryReference& target) {
  this->write_rm(Operation::SETNO, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setb(const MemoryReference& target) {
  this->write_rm(Operation::SETB, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnae(const MemoryReference& target) {
  this->write_rm(Operation::SETNAE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setc(const MemoryReference& target) {
  this->write_rm(Operation::SETC, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnb(const MemoryReference& target) {
  this->write_rm(Operation::SETNB, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setae(const MemoryReference& target) {
  this->write_rm(Operation::SETAE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnc(const MemoryReference& target) {
  this->write_rm(Operation::SETNC, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setz(const MemoryReference& target) {
  this->write_rm(Operation::SETZ, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_sete(const MemoryReference& target) {
  this->write_rm(Operation::SETE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnz(const MemoryReference& target) {
  this->write_rm(Operation::SETNZ, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setne(const MemoryReference& target) {
  this->write_rm(Operation::SETNE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setbe(const MemoryReference& target) {
  this->write_rm(Operation::SETBE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setna(const MemoryReference& target) {
  this->write_rm(Operation::SETNA, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnbe(const MemoryReference& target) {
  this->write_rm(Operation::SETNBE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_seta(const MemoryReference& target) {
  this->write_rm(Operation::SETA, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_sets(const MemoryReference& target) {
  this->write_rm(Operation::SETS, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setns(const MemoryReference& target) {
  this->write_rm(Operation::SETNS, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setp(const MemoryReference& target) {
  this->write_rm(Operation::SETP, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setpe(const MemoryReference& target) {
  this->write_rm(Operation::SETPE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnp(const MemoryReference& target) {
  this->write_rm(Operation::SETNP, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setpo(const MemoryReference& target) {
  this->write_rm(Operation::SETPO, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setl(const MemoryReference& target) {
  this->write_rm(Operation::SETL, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnge(const MemoryReference& target) {
  this->write_rm(Operation::SETNGE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnl(const MemoryReference& target) {
  this->write_rm(Operation::SETNL, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setge(const MemoryReference& target) {
  this->write_rm(Operation::SETGE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setle(const MemoryReference& target) {
  this->write_rm(Operation::SETLE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setng(const MemoryReference& target) {
  this->write_rm(Operation::SETNG, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setnle(const MemoryReference& target) {
  this->write_rm(Operation::SETNLE, target, 0, OperandSize::Byte);
}

void AMD64Assembler::write_setg(const MemoryReference& target) {
  this->write_rm(Operation::SETG, target, 0, OperandSize::Byte);
}



void AMD64Assembler::write(const string& data) {
  this->stream.emplace_back(data);
}

string AMD64Assembler::assemble(bool skip_missing_labels) {
  string code;

  // general strategy: assemble everything in order. for backward jumps, we know
  // exactly what the offset will be, so just use the right opcode. for forward
  // jumps, compute the offset as if all intervening jumps are 32-bit jumps, and
  // then backpatch the offset appropriately. this will waste space in some edge
  // cases but I'm lazy

  size_t stream_location = 0;
  auto label_it = this->labels.begin();
  for (auto stream_it = this->stream.begin(); stream_it != this->stream.end(); stream_it++) {
    const auto& item = *stream_it;

    // if there's a label at this location, set its memory location
    if ((label_it != this->labels.end()) &&
        (label_it->stream_location == stream_location)) {
      label_it->byte_location = code.size();

      // if there are patches waiting, perform them now that the label's
      // location is determined
      for (const auto& patch : label_it->patches) {
        // 8-bit patch
        if (!patch.is_32bit) {
          int64_t offset = static_cast<int64_t>(label_it->byte_location)
              - (static_cast<int64_t>(patch.where) + 1);
          if ((offset < -0x80) || (offset > 0x7F)) {
            throw runtime_error("8-bit patch location too far away");
          }
          *reinterpret_cast<int8_t*>(&code[patch.where]) = static_cast<int8_t>(offset);

        // 32-bit patch
        } else {
          int64_t offset = static_cast<int64_t>(label_it->byte_location)
              - (static_cast<int64_t>(patch.where) + 4);
          if ((offset < -0x80000000LL) || (offset > 0x7FFFFFFFLL)) {
            throw runtime_error("32-bit patch location too far away");
          }
          *reinterpret_cast<int32_t*>(&code[patch.where]) = static_cast<int32_t>(offset);
        }
      }
      label_it->patches.clear();
      label_it++;
    }

    // if this stream item is a jump opcode, find the relevant label
    if (item.relative_jump_opcode8 || item.relative_jump_opcode32) {
      Label* label = NULL;
      try {
        label = this->name_to_label.at(item.data);
      } catch (const out_of_range& e) {
        if (!skip_missing_labels) {
          throw runtime_error("nonexistent label: " + item.data);
        }
      }

      if (label) {
        // if the label's address is known, we can easily write a jump opcode
        if (label->byte_location <= code.size()) {
          code += this->generate_jmp(item.relative_jump_opcode8,
              item.relative_jump_opcode32, code.size(),
              label->byte_location);

        // else, we have to estimate how far away the label will be
        } else {

          // find the target label
          auto target_label_it = label_it;
          for (; target_label_it != this->labels.end(); target_label_it++) {
            if (target_label_it->name == item.data) {
              break;
            }
          }
          if (target_label_it == this->labels.end()) {
            throw runtime_error("label not found in forward stream: " + item.data);
          }
          size_t target_stream_location = target_label_it->stream_location;

          // find the maximum number of bytes between here and there
          int64_t max_displacement = 0;
          size_t where_stream_location = stream_location;
          for (auto where_it = stream_it + 1;
               (where_it != this->stream.end()) && (where_stream_location < target_stream_location);
               where_it++, where_stream_location++) {
            if (where_it->relative_jump_opcode8 || where_it->relative_jump_opcode32) {
              // assume it's a 32-bit jump
              max_displacement += 5 + (where_it->relative_jump_opcode32 > 0xFF);
            } else {
              max_displacement += where_it->data.size();
            }
          }

          // generate a bogus forward jmp opcode, and the appropriate patches
          code += this->generate_jmp(item.relative_jump_opcode8,
              item.relative_jump_opcode32, code.size(),
              code.size() + max_displacement);
          if ((max_displacement < 0x80) && item.relative_jump_opcode8) {
            target_label_it->patches.emplace_back(code.size() - 1, false);
          } else {
            target_label_it->patches.emplace_back(code.size() - 4, true);
          }
        }
      }

    // this item is not a jump opcode; just stick it in the buffer
    } else {
      code += item.data;
    }

    stream_location++;
  }

  // bugcheck: make sure there are no patches waiting
  for (const auto& label : this->labels) {
    if (!label.patches.empty()) {
      throw logic_error("some patches were not applied");
    }
  }

  this->name_to_label.clear();
  this->labels.clear();
  this->stream.clear();

  return code;
}

AMD64Assembler::StreamItem::StreamItem(const std::string& data,
    Operation opcode8, Operation opcode32) : data(data),
    relative_jump_opcode8(opcode8), relative_jump_opcode32(opcode32) { }

AMD64Assembler::Label::Patch::Patch(size_t where, bool is_32bit) : where(where),
    is_32bit(is_32bit) { }

AMD64Assembler::Label::Label(const std::string& name, size_t stream_location) :
    name(name), stream_location(stream_location),
    byte_location(0xFFFFFFFFFFFFFFFF) { }
