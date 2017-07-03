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
  print_data(stderr, code);

  CodeBuffer buf;
  void* function = buf.append(code);

  int64_t data = 0x0102030405060708;
  int64_t (*fn)(int64_t*) = reinterpret_cast<int64_t (*)(int64_t*)>(function);

  // the function should have returned 0xFEFDFCFBFAF93F7E and set the data
  // variable to that value also
  assert(fn(&data) == 0xFEFDFCFBFAF93F7E);
  assert(data == 0xFEFDFCFBFAF93F7E);
}

int main(int argc, char** argv) {
  test_trivial_function();

  // TODO: write jump test

  printf("-- all tests passed\n");
  return 0;
}
