#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>

#include <phosg/Strings.hh>

#include "AMD64Assembler.hh"
#include "CodeBuffer.hh"

using namespace std;


void test_trivial_function() {
  printf("-- trivial function\n");

  AMD64Assembler as;

  as.write_push(Register::RBP);
  as.write_mov(MemoryReference(Register::RBP), MemoryReference(Register::RSP));

  as.write_mov(MemoryReference(Register::RDX), MemoryReference(Register::RDI, 0));

  as.write_not(MemoryReference(Register::RDX));

  as.write_test(MemoryReference(Register::RDX), MemoryReference(Register::RDX));
  as.write_setz(MemoryReference(Register::DH));

  as.write_mov(MemoryReference(Register::R10), MemoryReference(Register::RDX));

  as.write_test(MemoryReference(Register::R10), MemoryReference(Register::R10));
  as.write_setz(MemoryReference(Register::R10B));

  as.write_xor(MemoryReference(Register::R10), 0x3F3F);
  as.write_xor(MemoryReference(Register::R10), 0x40);
  as.write_xor(MemoryReference(Register::R10B), 0x01, OperandSize::Byte);

  as.write_mov(MemoryReference(Register::RAX), MemoryReference(Register::R10));

  as.write_mov(MemoryReference(Register::RDI, 0), MemoryReference(Register::RAX));

  as.write_pop(Register::RBP);
  as.write_ret();

  string code = as.assemble();
  //print_data(stderr, code);

  //string disassembly = AMD64Assembler::disassemble(code.data(), code.size());
  //fprintf(stderr, "%s\n", disassembly.c_str());

  CodeBuffer buf;
  void* function = buf.append(code);

  int64_t data = 0x0102030405060708;
  int64_t (*fn)(int64_t*) = reinterpret_cast<int64_t (*)(int64_t*)>(function);

  // the function should have returned 0xFEFDFCFBFAF93F7E and set the data
  // variable to that value also
  assert(fn(&data) == 0xFEFDFCFBFAF93F7E);
  assert(data == 0xFEFDFCFBFAF93F7E);
}

void test_quicksort() {
  printf("-- quicksort\n");

  AMD64Assembler as;

  const Register rax = Register::RAX;
  const Register rcx = Register::RCX;
  const Register rdx = Register::RDX;
  const Register rsi = Register::RSI;
  const Register rdi = Register::RDI;
  const Register r8 = Register::R8;
  const Register r9 = Register::R9;

  // this mirrors the implementation in tests/quicksort.s
  as.write_mov(MemoryReference(rdx), MemoryReference(rdi));
  as.write_xor(MemoryReference(rdi), MemoryReference(rdi));
  as.write_dec(MemoryReference(rsi));
  as.write_label("0");
  as.write_cmp(MemoryReference(rdi), MemoryReference(rsi));
  as.write_jl("1");
  as.write_ret();
  as.write_label("1");
  as.write_lea(rcx, MemoryReference(rdi, 0, rsi));
  as.write_shr(MemoryReference(rcx), 1);
  as.write_mov(MemoryReference(rax), MemoryReference(rdx, 0, rsi, 8));
  as.write_xchg(rax, MemoryReference(rdx, 0, rcx, 8));
  as.write_mov(MemoryReference(rdx, 0, rsi, 8), MemoryReference(rax));
  as.write_lea(r8, MemoryReference(rdi, -1));
  as.write_mov(MemoryReference(r9), MemoryReference(rdi));
  as.write_label("2");
  as.write_inc(MemoryReference(r8));
  as.write_cmp(MemoryReference(r8), MemoryReference(rsi));
  as.write_jge("3");
  as.write_cmp(MemoryReference(rdx, 0, r8, 8), MemoryReference(rax));
  as.write_jge("2");
  as.write_mov(MemoryReference(rcx), MemoryReference(rdx, 0, r9, 8));
  as.write_xchg(rcx, MemoryReference(rdx, 0, r8, 8));
  as.write_mov(MemoryReference(rdx, 0, r9, 8), MemoryReference(rcx));
  as.write_inc(MemoryReference(r9));
  as.write_jmp("2");
  as.write_label("3");
  as.write_xchg(rax, MemoryReference(rdx, 0, r9, 8));
  as.write_mov(MemoryReference(rdx, 0, rsi, 8), MemoryReference(rax));
  as.write_push(MemoryReference(rsi));
  as.write_lea(rax, MemoryReference(r9, 1));
  as.write_push(rax);
  as.write_lea(rsi, MemoryReference(r9, -1));
  as.write_call("0");
  as.write_pop(rdi);
  as.write_pop(rsi);
  as.write_jmp("0");

  string code = as.assemble();
  //print_data(stderr, code);

  //string disassembly = AMD64Assembler::disassemble(code.data(), code.size());
  //fprintf(stderr, "%s\n", disassembly.c_str());

  CodeBuffer buf;
  void* function = buf.append(code);
  int64_t (*quicksort)(int64_t*, int64_t) = reinterpret_cast<int64_t (*)(int64_t*, int64_t)>(function);

  vector<vector<int64_t>> cases = {
    {},
    {0},
    {6, 4, 2, 0, 3, 1, 7, 9, 8, 5},
    {-100, -10, -1, 0, 1, 10, 100},
    {100, 10, 1, 0, -1, -10, -100},
  };
  for (auto& this_case : cases) {
    int64_t* data = const_cast<int64_t*>(this_case.data());

    quicksort(data, this_case.size());

    fprintf(stderr, "---- sorted:");
    for (uint64_t x = 0; x < this_case.size(); x++) {
      fprintf(stderr, " %" PRId64, data[x]);
    }
    fputc('\n', stderr);

    for (uint64_t x = 1; x < this_case.size(); x++) {
      assert(data[x - 1] < data[x]);
    }
  }
}

int main(int argc, char** argv) {
  test_trivial_function();
  test_quicksort();

  printf("-- all tests passed\n");
  return 0;
}
