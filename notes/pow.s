; exponentiation algorithm by repeated squaring
; this is O(log(exponent value))

; int64_t ipow(int64_t base, int64_t exponent);

.intel_syntax noprefix

_ipow:
  mov rax, 1

_ipow_again:
  test rsi, 1
  jz _ipow_skip_base
  imul rax, rdi
_ipow_skip_base:
  imul rdi, rdi
  shr rsi, 1
  jnz _ipow_again

  ret
