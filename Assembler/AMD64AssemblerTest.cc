#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/mman.h>

#include <phosg/Hash.hh>
#include <phosg/Strings.hh>

#include "AMD64Assembler.hh"
#include "CodeBuffer.hh"

using namespace std;


static void* assemble(CodeBuffer& code, AMD64Assembler& as) {
  multimap<size_t, string> compiled_labels;
  unordered_set<size_t> patch_offsets;
  string data = as.assemble(patch_offsets, &compiled_labels);
  void* ret = code.append(data, &patch_offsets);

  //print_data(stderr, ret, data.size(), reinterpret_cast<uint64_t>(ret));
  //string disassembly = AMD64Assembler::disassemble(ret, data.size());
  //fprintf(stderr, "%s\n", disassembly.c_str());

  return ret;
}


void test_trivial_function() {
  printf("-- trivial function\n");

  AMD64Assembler as;
  CodeBuffer code;

  as.write_push(rbp);
  as.write_mov(rbp, rsp);

  as.write_mov(rdx, MemoryReference(rdi, 0));

  as.write_not(rdx);

  as.write_test(rdx, rdx);
  as.write_setz(dh);

  as.write_mov(r10, rdx);

  as.write_test(r10, r10);
  as.write_setz(r10b);

  as.write_xor(r10, 0x3F3F);
  as.write_xor(r10, 0x40);
  as.write_xor(r10b, 0x01, OperandSize::Byte);

  as.write_mov(rax, r10);

  as.write_mov(MemoryReference(rdi, 0), rax);

  as.write_pop(rbp);
  as.write_ret();

  void* function = assemble(code, as);

  uint64_t data = 0x0102030405060708;
  uint64_t (*fn)(uint64_t*) = reinterpret_cast<uint64_t (*)(uint64_t*)>(function);

  // the function should have returned 0xFEFDFCFBFAF93F7E and set the data
  // variable to that value also
  assert(fn(&data) == 0xFEFDFCFBFAF93F7E);
  assert(data == 0xFEFDFCFBFAF93F7E);
}


void test_pow() {
  printf("-- pow\n");

  AMD64Assembler as;
  CodeBuffer code;

  // this mirrors the implementation in notes/pow.s
  as.write_mov(rax, 1);
  as.write_label("_pow_again");
  as.write_test(rsi, 1);
  as.write_jz("_pow_skip_base");
  as.write_imul(rax.base_register, rdi);
  as.write_label("_pow_skip_base");
  as.write_imul(rdi.base_register, rdi);
  as.write_shr(rsi, 1);
  as.write_jnz("_pow_again");
  as.write_ret();

  void* function = assemble(code, as);
  int64_t (*pow)(int64_t, int64_t) = reinterpret_cast<int64_t (*)(int64_t, int64_t)>(function);

  assert(pow(0, 0) == 1);
  assert(pow(0, 1) == 0);
  assert(pow(0, 10) == 0);
  assert(pow(0, 100) == 0);
  assert(pow(1, 0) == 1);
  assert(pow(1, 1) == 1);
  assert(pow(1, 10) == 1);
  assert(pow(1, 100) == 1);
  assert(pow(2, 0) == 1);
  assert(pow(2, 1) == 2);
  assert(pow(2, 10) == 1024);
  assert(pow(2, 20) == 1048576);
  assert(pow(2, 30) == 1073741824);
  assert(pow(3, 0) == 1);
  assert(pow(3, 1) == 3);
  assert(pow(3, 2) == 9);
  assert(pow(3, 3) == 27);
  assert(pow(3, 4) == 81);
  assert(pow(-1, 0) == 1);
  assert(pow(-1, 1) == -1);
  assert(pow(-1, 2) == 1);
  assert(pow(-1, 3) == -1);
  assert(pow(-1, 4) == 1);
  assert(pow(-2, 0) == 1);
  assert(pow(-2, 1) == -2);
  assert(pow(-2, 10) == 1024);
  assert(pow(-2, 20) == 1048576);
  assert(pow(-2, 30) == 1073741824);
  assert(pow(-3, 0) == 1);
  assert(pow(-3, 1) == -3);
  assert(pow(-3, 2) == 9);
  assert(pow(-3, 3) == -27);
  assert(pow(-3, 4) == 81);
}


void test_quicksort() {
  printf("-- quicksort\n");

  AMD64Assembler as;
  CodeBuffer code;

  // this mirrors the implementation in notes/quicksort.s
  as.write_mov(rdx, rdi);
  as.write_xor(rdi, rdi);
  as.write_dec(rsi);
  as.write_label("0");
  as.write_cmp(rdi, rsi);
  as.write_jl("1");
  as.write_ret();
  as.write_label("1");
  as.write_lea(rcx, MemoryReference(rdi, 0, rsi));
  as.write_shr(rcx, 1);
  as.write_mov(rax, MemoryReference(rdx, 0, rsi, 8));
  as.write_xchg(rax, MemoryReference(rdx, 0, rcx, 8));
  as.write_mov(MemoryReference(rdx, 0, rsi, 8), rax);
  as.write_lea(r8, MemoryReference(rdi, -1));
  as.write_mov(r9, rdi);
  as.write_label("2");
  as.write_inc(r8);
  as.write_cmp(r8, rsi);
  as.write_jge("3");
  as.write_cmp(MemoryReference(rdx, 0, r8, 8), rax);
  as.write_jge("2");
  as.write_mov(rcx, MemoryReference(rdx, 0, r9, 8));
  as.write_xchg(rcx, MemoryReference(rdx, 0, r8, 8));
  as.write_mov(MemoryReference(rdx, 0, r9, 8), rcx);
  as.write_inc(r9);
  as.write_jmp("2");
  as.write_label("3");
  as.write_xchg(rax, MemoryReference(rdx, 0, r9, 8));
  as.write_mov(MemoryReference(rdx, 0, rsi, 8), rax);
  as.write_push(rsi);
  as.write_lea(rax, MemoryReference(r9, 1));
  as.write_push(rax);
  as.write_lea(rsi, MemoryReference(r9, -1));
  as.write_call("0");
  as.write_pop(rdi);
  as.write_pop(rsi);
  as.write_jmp("0");

  void* function = assemble(code, as);
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


void test_hash_fnv1a64() {
  printf("-- hash_fnv1a64\n");

  AMD64Assembler as;
  CodeBuffer code;

  // this mirrors the implementation in notes/hash.s
  as.write_mov(rdx, 0xCBF29CE484222325);
  as.write_add(rsi, rdi);
  as.write_xor(rax, rax);
  as.write_mov(rcx, 0x00000100000001B3);
  as.write_jmp("check_end");

  as.write_label("continue");
  as.write_mov(al, MemoryReference(rdi, 0), OperandSize::Byte);
  as.write_xor(rdx, rax);
  as.write_imul(rdx, rcx);
  as.write_inc(rdi);
  as.write_label("check_end");
  as.write_cmp(rdi, rsi);
  as.write_jne("continue");

  as.write_mov(rax, rdx);
  as.write_ret();

  void* function = assemble(code, as);
  uint64_t (*hash)(const void*, size_t) = reinterpret_cast<uint64_t (*)(const void*, size_t)>(function);

  assert(hash("", 0) == fnv1a64("", 0));
  assert(hash("omg", 3) == fnv1a64("omg", 3));
  // we intentionally include the \0 at the end of the string here
  assert(hash("0123456789", 11) == fnv1a64("0123456789", 11));
}


void test_float_move_load_multiply() {
  printf("-- floating move + load + multiply\n");

  AMD64Assembler as;
  CodeBuffer code;

  as.write_movq_from_xmm(rax, xmm0);
  as.write_movq_to_xmm(xmm0, rax);
  as.write_movsd(xmm1, MemoryReference(rdi, 0));
  as.write_mulsd(xmm0, xmm1);
  as.write_ret();

  void* function = assemble(code, as);
  double (*mul)(double*, double) = reinterpret_cast<double (*)(double*, double)>(function);

  double x = 1.5;
  assert(mul(&x, 3.0) == 4.5);
}


void test_float_neg() {
  printf("-- floating negative\n");

  AMD64Assembler as;
  CodeBuffer code;

  as.write_movq_from_xmm(rax, xmm0);
  as.write_rol(rax, 1);
  as.write_xor(rax, 1);
  as.write_ror(rax, 1);
  as.write_movq_to_xmm(xmm0, rax);
  as.write_ret();

  void* function = assemble(code, as);
  double (*neg)(double) = reinterpret_cast<double (*)(double)>(function);

  assert(neg(1.5) == -1.5);
}


void test_absolute_patches() {
  printf("-- absolute patches\n");

  AMD64Assembler as;
  CodeBuffer code;

  as.write_mov(rax, "label1");
  as.write_label("label1");
  as.write_ret();

  void* function = assemble(code, as);
  size_t (*fn)() = reinterpret_cast<size_t (*)()>(function);

  // the movabs opcode is 10 bytes long
  assert(fn() == reinterpret_cast<size_t>(function) + 10);
}


int main(int argc, char** argv) {
  test_trivial_function();
  test_pow();
  test_hash_fnv1a64();
  test_quicksort();
  test_float_move_load_multiply();
  test_float_neg();
  test_absolute_patches();

  printf("-- all tests passed\n");
  return 0;
}
