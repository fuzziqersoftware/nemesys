#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>

#include <phosg/Strings.hh>

#include "AMD64Assembler.hh"

using namespace std;


int main(int argc, char** argv) {
  string function;
  function += generate_push(Register::RBP);
  function += generate_mov(MemoryReference(Register::RBP),
      MemoryReference(Register::RSP), OperandSize::QuadWord);

  function += generate_mov(MemoryReference(Register::RDX),
      MemoryReference(Register::RDI, 0), OperandSize::QuadWord);

  function += generate_not(MemoryReference(Register::RDX),
      OperandSize::QuadWord);

  function += generate_test(MemoryReference(Register::RDX),
      MemoryReference(Register::RDX), OperandSize::QuadWord);
  function += generate_setz(MemoryReference(Register::DH));

  function += generate_mov(MemoryReference(Register::R10),
      MemoryReference(Register::RDX), OperandSize::QuadWord);

  function += generate_test(MemoryReference(Register::R10),
      MemoryReference(Register::R10), OperandSize::QuadWord);
  function += generate_setz(MemoryReference(Register::R10B));

  function += generate_xor(MemoryReference(Register::R10), 0x3F3F,
      OperandSize::QuadWord);
  function += generate_xor(MemoryReference(Register::R10), 0x40,
      OperandSize::QuadWord);
  function += generate_xor(MemoryReference(Register::R10B), 0x01,
      OperandSize::Byte);

  function += generate_mov(MemoryReference(Register::RAX),
      MemoryReference(Register::R10), OperandSize::QuadWord);

  function += generate_mov(MemoryReference(Register::RDI, 0),
      MemoryReference(Register::RAX), OperandSize::QuadWord);

  function += generate_pop(Register::RBP);
  function += generate_ret();

  function += generate_mov(Register::RDX, 0x0102030405060708,
      OperandSize::QuadWord);
  function += generate_call(MemoryReference(Register::RDX));

  print_data(stdout, function.data(), function.size());

  void* executable_page = mmap(NULL, 0x1000,
      PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (executable_page == MAP_FAILED) {
    fprintf(stdout, "can\'t map executable page\n");
    return 1;
  }

  fprintf(stdout, "copying function to executable memory\n");
  memcpy(executable_page, function.data(), function.size());
  int64_t data = 0x0102030405060708;
  fprintf(stdout, "calling function with 0x0102030405060708 @ %p\n", &data);
  int64_t (*fn)(int64_t*) = reinterpret_cast<int64_t (*)(int64_t*)>(executable_page);
  fprintf(stdout, "ret  = 0x%" PRIX64 "\n", fn(&data));
  fprintf(stdout, "data = 0x%" PRIX64 "\n", data);

  return 0;
}
