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
  R8D = 8,
  R8W = 8,
  R8B = 8,

  R9 = 9,
  R9D = 9,
  R9W = 9,
  R9B = 9,

  R10 = 10,
  R10D = 10,
  R10W = 10,
  R10B = 10,

  R11 = 11,
  R11D = 11,
  R11W = 11,
  R11B = 11,

  R12 = 12,
  R12D = 12,
  R12W = 12,
  R12B = 12,

  R13 = 13,
  R13D = 13,
  R13W = 13,
  R13B = 13,

  R14 = 14,
  R14D = 14,
  R14W = 14,
  R14B = 14,

  R15 = 15,
  R15D = 15,
  R15W = 15,
  R15B = 15,

  Count = 16,
};

enum Operation {
  ADD_STORE8 = 0x00,
  ADD_STORE  = 0x01,
  ADD_LOAD8  = 0x02,
  ADD_LOAD   = 0x03,
  OR_STORE8  = 0x08,
  OR_STORE   = 0x09,
  OR_LOAD8   = 0x0A,
  OR_LOAD    = 0x0B,
  ADC_STORE8 = 0x10,
  ADC_STORE  = 0x11,
  ADC_LOAD8  = 0x12,
  ADC_LOAD   = 0x13,
  SBB_STORE8 = 0x18,
  SBB_STORE  = 0x19,
  SBB_LOAD8  = 0x1A,
  SBB_LOAD   = 0x1B,
  AND_STORE8 = 0x20,
  AND_STORE  = 0x21,
  AND_LOAD8  = 0x22,
  AND_LOAD   = 0x23,
  SUB_STORE8 = 0x28,
  SUB_STORE  = 0x29,
  SUB_LOAD8  = 0x2A,
  SUB_LOAD   = 0x2B,
  XOR_STORE8 = 0x30,
  XOR_STORE  = 0x31,
  XOR_LOAD8  = 0x32,
  XOR_LOAD   = 0x33,
  CMP_STORE8 = 0x38,
  CMP_STORE  = 0x39,
  CMP_LOAD8  = 0x3A,
  CMP_LOAD   = 0x3B,
  JO8        = 0x70,
  JNO8       = 0x71,
  JB8        = 0x72,
  JNAE8      = 0x72,
  JC8        = 0x72,
  JNB8       = 0x73,
  JAE8       = 0x73,
  JNC8       = 0x73,
  JZ8        = 0x74,
  JE8        = 0x74,
  JNZ8       = 0x75,
  JNE8       = 0x75,
  JBE8       = 0x76,
  JNA8       = 0x76,
  JNBE8      = 0x77,
  JA8        = 0x77,
  JS8        = 0x78,
  JNS8       = 0x79,
  JP8        = 0x7A,
  JPE8       = 0x7A,
  JNP8       = 0x7B,
  JPO8       = 0x7B,
  JL8        = 0x7C,
  JNGE8      = 0x7C,
  JNL8       = 0x7D,
  JGE8       = 0x7D,
  JLE8       = 0x7E,
  JNG8       = 0x7E,
  JNLE8      = 0x7F,
  JG8        = 0x7F,
  MATH8_IMM8 = 0x80,
  MATH_IMM32 = 0x81,
  MATH_IMM8  = 0x83,
  TEST       = 0x85,
  MOV_STORE8 = 0x88,
  MOV_STORE  = 0x89,
  MOV_LOAD8  = 0x8A,
  MOV_LOAD   = 0x8B,
  NOT_NEG    = 0xF7,
  CALL_JMP_ABS = 0xFF, // also INC/DEC/PUSH_RM; these aren't implemented
  JO         = 0x0F80,
  JNO        = 0x0F81,
  JB         = 0x0F82,
  JNAE       = 0x0F82,
  JC         = 0x0F82,
  JNB        = 0x0F83,
  JAE        = 0x0F83,
  JNC        = 0x0F83,
  JZ         = 0x0F84,
  JE         = 0x0F84,
  JNZ        = 0x0F85,
  JNE        = 0x0F85,
  JBE        = 0x0F86,
  JNA        = 0x0F86,
  JNBE       = 0x0F87,
  JA         = 0x0F87,
  JS         = 0x0F88,
  JNS        = 0x0F89,
  JP         = 0x0F8A,
  JPE        = 0x0F8A,
  JNP        = 0x0F8B,
  JPO        = 0x0F8B,
  JL         = 0x0F8C,
  JNGE       = 0x0F8C,
  JNL        = 0x0F8D,
  JGE        = 0x0F8D,
  JLE        = 0x0F8E,
  JNG        = 0x0F8E,
  JNLE       = 0x0F8F,
  JG         = 0x0F8F,
  SETO       = 0x0F90,
  SETNO      = 0x0F91,
  SETB       = 0x0F92,
  SETNAE     = 0x0F92,
  SETC       = 0x0F92,
  SETNB      = 0x0F93,
  SETAE      = 0x0F93,
  SETNC      = 0x0F93,
  SETZ       = 0x0F94,
  SETE       = 0x0F94,
  SETNZ      = 0x0F95,
  SETNE      = 0x0F95,
  SETBE      = 0x0F96,
  SETNA      = 0x0F96,
  SETNBE     = 0x0F97,
  SETA       = 0x0F97,
  SETS       = 0x0F98,
  SETNS      = 0x0F99,
  SETP       = 0x0F9A,
  SETPE      = 0x0F9A,
  SETNP      = 0x0F9B,
  SETPO      = 0x0F9B,
  SETL       = 0x0F9C,
  SETNGE     = 0x0F9C,
  SETNL      = 0x0F9D,
  SETGE      = 0x0F9D,
  SETLE      = 0x0F9E,
  SETNG      = 0x0F9E,
  SETNLE     = 0x0F9F,
  SETG       = 0x0F9F,
};

enum OperandSize {
  Byte = 0,
  Word = 1,
  DoubleWord = 2,
  QuadWord = 3,
};


struct MemoryReference {
  Register base_register;
  Register index_register;
  int8_t field_size; // if 0, this is a register reference (not memory)
  int64_t offset;

  MemoryReference(Register base_register, int64_t offset,
      Register index_register = Register::None, uint8_t field_size = 1);
  MemoryReference(Register base_register);
};

// stack opcodes
std::string generate_push(Register r);
std::string generate_pop(Register r);

// move opcodes
std::string generate_mov(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_mov(Register reg, int64_t value, OperandSize size);

// control flow opcodes
std::string generate_jmp(const MemoryReference& mem);
std::string generate_jmp(int64_t offset);
std::string generate_call(const MemoryReference& mem);
std::string generate_call(int64_t offset);
std::string generate_ret(uint16_t stack_bytes = 0);
std::string generate_jo(int64_t offset);
std::string generate_jno(int64_t offset);
std::string generate_jb(int64_t offset);
std::string generate_jnae(int64_t offset);
std::string generate_jc(int64_t offset);
std::string generate_jnb(int64_t offset);
std::string generate_jae(int64_t offset);
std::string generate_jnc(int64_t offset);
std::string generate_jz(int64_t offset);
std::string generate_je(int64_t offset);
std::string generate_jnz(int64_t offset);
std::string generate_jne(int64_t offset);
std::string generate_jbe(int64_t offset);
std::string generate_jna(int64_t offset);
std::string generate_jnbe(int64_t offset);
std::string generate_ja(int64_t offset);
std::string generate_js(int64_t offset);
std::string generate_jns(int64_t offset);
std::string generate_jp(int64_t offset);
std::string generate_jpe(int64_t offset);
std::string generate_jnp(int64_t offset);
std::string generate_jpo(int64_t offset);
std::string generate_jl(int64_t offset);
std::string generate_jnge(int64_t offset);
std::string generate_jnl(int64_t offset);
std::string generate_jge(int64_t offset);
std::string generate_jle(int64_t offset);
std::string generate_jng(int64_t offset);
std::string generate_jnle(int64_t offset);
std::string generate_jg(int64_t offset);

// math opcodes
std::string generate_add(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_add(const MemoryReference& to, int64_t value,
    OperandSize size);
std::string generate_or(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_or(const MemoryReference& to, int64_t value,
    OperandSize size);
std::string generate_adc(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_adc(const MemoryReference& to, int64_t value,
    OperandSize size);
std::string generate_sbb(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_sbb(const MemoryReference& to, int64_t value,
    OperandSize size);
std::string generate_and(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_and(const MemoryReference& to, int64_t value,
    OperandSize size);
std::string generate_sub(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_sub(const MemoryReference& to, int64_t value,
    OperandSize size);
std::string generate_xor(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_xor(const MemoryReference& to, int64_t value,
    OperandSize size);
std::string generate_cmp(const MemoryReference& to, const MemoryReference& from,
    OperandSize size);
std::string generate_cmp(const MemoryReference& to, int64_t value,
    OperandSize size);
std::string generate_not(const MemoryReference& target, OperandSize size);
std::string generate_neg(const MemoryReference& target, OperandSize size);

// comparison opcodes
std::string generate_test(const MemoryReference& a, const MemoryReference& b,
    OperandSize size);
std::string generate_seto(const MemoryReference& target);
std::string generate_setno(const MemoryReference& target);
std::string generate_setb(const MemoryReference& target);
std::string generate_setnae(const MemoryReference& target);
std::string generate_setc(const MemoryReference& target);
std::string generate_setnb(const MemoryReference& target);
std::string generate_setae(const MemoryReference& target);
std::string generate_setnc(const MemoryReference& target);
std::string generate_setz(const MemoryReference& target);
std::string generate_sete(const MemoryReference& target);
std::string generate_setnz(const MemoryReference& target);
std::string generate_setne(const MemoryReference& target);
std::string generate_setbe(const MemoryReference& target);
std::string generate_setna(const MemoryReference& target);
std::string generate_setnbe(const MemoryReference& target);
std::string generate_seta(const MemoryReference& target);
std::string generate_sets(const MemoryReference& target);
std::string generate_setns(const MemoryReference& target);
std::string generate_setp(const MemoryReference& target);
std::string generate_setpe(const MemoryReference& target);
std::string generate_setnp(const MemoryReference& target);
std::string generate_setpo(const MemoryReference& target);
std::string generate_setl(const MemoryReference& target);
std::string generate_setnge(const MemoryReference& target);
std::string generate_setnl(const MemoryReference& target);
std::string generate_setge(const MemoryReference& target);
std::string generate_setle(const MemoryReference& target);
std::string generate_setng(const MemoryReference& target);
std::string generate_setnle(const MemoryReference& target);
std::string generate_setg(const MemoryReference& target);
