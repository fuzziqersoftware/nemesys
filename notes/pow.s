.intel_syntax noprefix

# exponentiation algorithm by repeated squaring. this is O(log(exponent value))

# in c: int64_t ipow(int64_t base, int64_t exponent);

.globl _ipow
_ipow:
  mov   rax, 1

_ipow_again:
  test  rsi, 1
  jz    _ipow_skip_base
  imul  rax, rdi
_ipow_skip_base:
  imul  rdi, rdi
  shr   rsi, 1
  jnz   _ipow_again

  ret
