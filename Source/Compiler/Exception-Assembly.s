.intel_syntax noprefix

# exception unwinding entry point from c code. this function searches the
# exception blocks for one that matches the active exception and rewinds the
# stack to that point. this function never returns to the point where it was
# called - it returns to somewhere further up the call stack.
.globl raise_python_exception
.globl _raise_python_exception
raise_python_exception:
_raise_python_exception:
  # c entry point for raising a nemesys exception. exc block ptr is rdi, exc
  # object is rsi. just move them into the right places and go to
  # __unwind_exception_internal. if the exception block or exception object is
  # NULL, return without doing anything
  test rdi, rdi
  jnz _raise_python_exception__block_valid
  ret
_raise_python_exception__block_valid:
  test rsi, rsi
  jnz _raise_python_exception__exc_valid
  ret
_raise_python_exception__exc_valid:
  mov r14, rdi
  mov r15, rsi

# exception unwinding entry point when called from nemesys code. note that this
# function does not follow the system v calling convention that we adhere to in
# the rest of this project! it expects its arguments in r14 (the exception block
# chain) and r15 (the active exception).
.globl _unwind_exception_internal
.globl __unwind_exception_internal
_unwind_exception_internal:
__unwind_exception_internal:
  # get the exception class id
  mov rdx, [r15 + 16]

  # search for an exception spec that matches this class id. we have the
  # guarantee that something in the exception block will match, since all
  # exception blocks end with a spec that matches everything. use r8 as the
  # exception spec pointer
  lea r8, [r14 + 40]

__unwind_exception_internal__check_spec_match:
  mov r9, [r8 + 8]

  # if this spec has no class ids, then it matches everything
  test r9, r9
  jz __unwind_exception_internal__restore_block
  dec r9

__unwind_exception_internal__check_spec_match__check_class_id:
  # check if the class id (rax) matches the active exception class (rdx);
  # restore the block if it does
  mov rax, [r8 + 8 * r9 + 16]
  cmp rax, rdx
  jz __unwind_exception_internal__restore_block

  # check the next class id in this spec. we have to use sub here; dec does not
  # affect the carry flag
  sub r9, 1
  jnc __unwind_exception_internal__check_spec_match__check_class_id

  # move on to the next exception spec
  mov r9, [r8 + 8]
  lea r8, [r8 + 8 * r9 + 16]
  jmp __unwind_exception_internal__check_spec_match

__unwind_exception_internal__restore_block:
  # load rsp and rbp from the exception block, remove the exception block from
  # the list in r14, and jump to the rip from the exception spec (r8)
  mov rsp, [r14 + 8]
  mov rbp, [r14 + 16]
  mov r12, [r14 + 24]
  mov r13, [r14 + 32]
  mov r14, [r14]
  jmp [r8]
