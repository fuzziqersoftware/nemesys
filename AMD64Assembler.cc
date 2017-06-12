#include "AMD64Assembler.hh"

#include <string>

using namespace std;


MemoryReference::MemoryReference(Register base_register,
    Register index_register, uint8_t field_size, int64_t offset) :
    is_memory_reference(true), base_register(base_register),
    index_register(index_register), field_size(field_size), offset(offset) { }
MemoryReference::MemoryReference(Register base_register,
    bool is_memory_reference) : is_memory_reference(is_memory_reference),
    base_register(base_register), index_register(Register::None), field_size(1),
    offset(0) { }

static inline bool is_extension_register(Register r) {
  int8_t rn = static_cast<int8_t>(r);
  return rn >= 8;
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

string generate_rm64(Operation op, const MemoryReference& to,
    const MemoryReference& from) {
  if (to.is_memory_reference && from.is_memory_reference) {
    throw invalid_argument("mov opcode can have at most one memory reference");
  }

  string ret;
  if (!to.is_memory_reference && !from.is_memory_reference) {
    // behavior = 3
    bool from_ext = is_extension_register(from.base_register);
    bool to_ext = is_extension_register(to.base_register);
    ret += 0x48 | (to_ext ? 0x04 : 0) | (from_ext ? 0x01 : 0);
    ret += 0x89;
    ret += static_cast<char>(0xC0 | ((from.base_register & 7) << 3) |
        (to.base_register & 7));
    return ret;
  }

  const MemoryReference* mem;
  Register reg;
  if (to.is_memory_reference) {
    // this is a store opcode; we only implement load below. fortunately they're
    // exactly the same except for a bit being flipped in the first byte
    reg = from.base_register;
    mem = &to;
  } else {
    reg = to.base_register;
    mem = &from;
  }

  // if there's no base register and no inde register, then it's just an addr
  if ((mem->index_register == Register::None) &&
      (mem->base_register == Register::None)) {
    throw invalid_argument("moves without registers not supported");
  }

  bool is_load = from.is_memory_reference;
  bool reg_ext = is_extension_register(to.base_register);
  bool mem_index_ext = is_extension_register(mem->index_register);
  bool mem_base_ext = is_extension_register(mem->base_register);

  uint8_t rm_byte = ((to.base_register & 7) << 3);
  uint8_t sib_byte = 0;

  // if an index register was given OR the base register is RSP, we'll have to
  // use the sib byte
  if (mem->index_register != Register::None) {
    rm_byte |= 0x04;
    sib_byte = ((mem->index_register & 7) << 3) | (mem->base_register & 7);
    if (mem->field_size == 1) {
      // nothing to do; m=0
    } else if (mem->field_size == 1) {
      sib_byte |= 0x40;
    } else if (mem->field_size == 1) {
      sib_byte |= 0x80;
    } else if (mem->field_size == 1) {
      sib_byte |= 0xC0;
    } else {
      throw invalid_argument("field_size must be 1, 2, 4, or 8");
    }
  }

  // if an offset was given, update the behavior appropriately
  if (mem->offset == 0) {
    // behavior is 0; nothing to do
  } else if ((mem->offset <= 0x7F) && (mem->offset >= -0x80)) {
    rm_byte |= 0x40;
    ret.append(reinterpret_cast<const char*>(&mem->offset), 1);
  } else if ((mem->offset <= 0x7FFFFFFF) && (mem->offset >= -0x80000000)) {
    rm_byte |= 0x80;
    ret.append(reinterpret_cast<const char*>(&mem->offset), 4);
  } else {
    throw invalid_argument("offset must fit in 32 bits");
  }

  // if no offset was given and the sib byte was not used and the base reg is
  // RSP, then add a fake offset of 0 to the opcode (this is because this case
  // is shadowed by a special case above)
  if ((mem->offset == 0) && ((rm_byte & 7) != 0x04) &&
      (mem->base_register == Register::RSP)) {
    rm_byte |= 0x40;
  }

  // fill in the ret string
  ret += 0x48 | (reg_ext ? 0x04 : 0) | (mem_index_ext ? 0x02 : 0) |
      (mem_base_ext ? 0x01 : 0);
  ret += 0x01 | (op << 3) | (is_load ? 0x02 : 0);
  ret += rm_byte;
  if ((rm_byte & 7) == 0x04) {
    ret += sib_byte;
  }
  if (rm_byte & 0x40) {
    ret.append(reinterpret_cast<const char*>(&mem->offset), 1);
  } else if (rm_byte & 0x80) {
    ret.append(reinterpret_cast<const char*>(&mem->offset), 4);
  }
  return ret;
}

string generate_mov_imm64(Register reg, uint64_t value) {
  if (value == 0) {
    // xor reg, reg
    MemoryReference r(reg);
    return generate_rm64(Operation::XOR, r, r);
  }

  // TODO: we can use C7 instead of B8 for small numbers
  string ret;
  ret += static_cast<char>(0x48 | (is_extension_register(reg) ? 0x01 : 0));
  ret += static_cast<char>(0xB8 + (reg & 7));
  ret.append(reinterpret_cast<const char*>(&value), 8);
  return ret;
}

string generate_jmp(int64_t offset) {
  string ret;
  if ((offset >= -0x80) && (offset <= 0x7F)) {
    ret += 0xEB;
    ret.append(reinterpret_cast<const char*>(&offset), 1);
  } else {
    ret += 0xE9;
    ret.append(reinterpret_cast<const char*>(&offset), 4);
  }
  return ret;
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
