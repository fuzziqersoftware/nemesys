; exponentiation algorithm by repeated squaring
; this is O(log(exponent value))

pow:
  ; rdi = base
  ; rsi = exponent

  mov rax, 1

_pow_again:
  test rsi, 1
  jz _pow_skip_base
  imul rax, rdi
_pow_skip_base:
  imul rdi, rdi
  shr rsi, 1
  jnz _pow_again

  ret
