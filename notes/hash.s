.intel_syntax noprefix

.globl fnv1a64

_fnv1a64_start:
  mov   rdx, 0xCBF29CE484222325

_fnv1a64_continue:
  add   rsi, rdi # rsi = end ptr
  xor   rax, rax
  mov   rcx, 0x00000100000001B3
  jmp   _fnv1a64_continue__check_end

_fnv1a64_continue__again:
  mov   al, [rdi]
  xor   rdx, rax
  imul  rdx, rcx
  inc   rdi
_fnv1a64_continue__check_end:
  cmp   rdi, rsi
  jne   _fnv1a64_continue__again

  mov   rax, rdx
  ret
