#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>

#include <phosg/Strings.hh>

#include "AMD64Assembler.hh"

using namespace std;


int main(int argc, char** argv) {
  string function;
  function += generate_push(Register::RBP);
  function += generate_rm64(Operation::MOV,
      MemoryReference(Register::RAX, false),
      MemoryReference(Register::RCX, false));
  function += generate_pop(Register::RBP);
  function += generate_ret();

  print_data(stdout, function.data(), function.size());

  void* executable_page = mmap(NULL, 0x1000,
      PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (executable_page == MAP_FAILED) {
    fprintf(stdout, "can\'t map executable page\n");
    return 1;
  }

  fprintf(stdout, "copying function to executable memory\n");
  memcpy(executable_page, function.data(), function.size());
  fprintf(stdout, "calling function with 0x0102030405060708\n");
  uint64_t (*fn)(uint64_t) = (uint64_t (*)(uint64_t))executable_page;
  fprintf(stdout, "ret = 0x%" PRIX64 "\n", fn(0x0102030405060708));

  return 0;
}
