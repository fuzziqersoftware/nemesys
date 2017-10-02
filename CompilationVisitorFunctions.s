.intel_syntax noprefix

// this function searches the exception blocks for one that matches the active
// exception (in r15) and rewinds the stack to that point. that this function
// does not follow the system v calling convention that we adhere to in the rest
// of this project! it expects its arguments in r14 (the exception block chain)
// and r15 (the active exception), and it never returns to the point where it
// was called - it returns to somewhere further up the call stack.
.globl __unwind_exception_internal
__unwind_exception_internal:
  // get the exception class id
  mov rdx, [r15 + 16]

  // search for an exception spec that matches this class id. we have the
  // guarantee that something in the exception block will match, since all
  // exception blocks end with a spec that matches everything. use r8 as the
  // exception spec pointer
  lea r8, [r14 + 24]

0: __unwind_exception_internal__check_spec_match:
  mov r9, [r8 + 8]

  // if this spec has no class ids, then it matches everything
  test r9, r9
  jz 2f // __unwind_exception_internal__restore_block
  dec r9

1: __unwind_exception_internal__check_spec_match__check_class_id:
  // check if the class id (rax) matches the active exception class (rdx);
  // restore the block if it does
  mov rax, [r8 + 8 * r9 + 16]
  cmp rax, rdx
  jz 2f // __unwind_exception_internal__restore_block

  // check the next class id in this spec. we have to use sub here; dec does not
  // affect the carry flag
  sub r9, 1
  jnc 1b // __unwind_exception_internal__check_spec_match__check_class_id

  // move on to the next exception spec
  mov r9, [r8 + 8]
  lea r8, [r8 + 8 * r9 + 16]
  jmp 0b // __unwind_exception_internal__check_spec_match

2: __unwind_exception_internal__restore_block:
  // load rsp and rbp from the exception block, remove the exception block from
  // the list in r14, and jump to the rip from the exception spec (r8)
  mov rsp, [r14 + 8]
  mov rbp, [r14 + 16]
  mov r14, [r14]
  jmp [r8]
