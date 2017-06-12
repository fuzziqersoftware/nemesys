#pragma once

#include <string>


enum Register {
  None = -1,
  RAX = 0,
  EAX = 0,
  AX = 0,
  AL = 0,

  RCX = 1,
  ECX = 1,
  CX = 1,
  CL = 1,

  RDX = 2,
  EDX = 2,
  DX = 2,
  DL = 2,

  RBX = 3,
  EBX = 3,
  BX = 3,
  BL = 3,

  RSP = 4,
  ESP = 4,
  SP = 4,
  AH = 4,

  RBP = 5,
  EBP = 5,
  BP = 5,
  CH = 5,

  RSI = 6,
  ESI = 6,
  SI = 6,
  DH = 6,

  RDI = 7,
  EDI = 7,
  DI = 7,
  BH = 7,

  R8 = 8,
  R9 = 9,
  R10 = 10,
  R11 = 11,
  R12 = 12,
  R13 = 13,
  R14 = 14,
  R15 = 15,
};

enum Operation {
  ADD = 0,
  OR = 1,
  ADC = 2,
  SBB = 3,
  AND = 4,
  SUB = 5,
  XOR = 6,
  CMP = 7,
  MOV = 17,
};


struct MemoryReference {
  bool is_memory_reference; // if false, only base_register is used
  Register base_register;
  Register index_register;
  int8_t field_size; // multiplier for index register
  int64_t offset;

  MemoryReference(Register base_register,
      Register index_register = Register::None, uint8_t field_size = 1,
      int64_t offset = 0);
  MemoryReference(Register base_register, bool is_memory_reference);
};

std::string generate_push(Register r);
std::string generate_pop(Register r);
std::string generate_rm64(Operation op, const MemoryReference& to,
    const MemoryReference& from);
std::string generate_mov_imm64(Register reg, uint64_t value);
std::string generate_jmp(int64_t offset);
std::string generate_call(int64_t offset);
std::string generate_ret(uint16_t stack_bytes = 0);
