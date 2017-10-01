.intel_syntax noprefix

// # all try blocks have a finally block, even if it's not defined in the code
// # let N be the number of except clauses on the try block
// try:
//   # stack-allocate one exception block on the stack with exc_class_id = 0,
//   #     pointing to the finally block
//   # stack-allocate N exception blocks for except clauses in reverse order
//   if should_raise:
//     raise KeyError()  # allocate object, set r15, call unwind_exception
//   # remove the exception block structs from the stack
//   # if there's an else block, jump there
//   # if there's a finally block, jump there
//   # jump to end of suite chain
// except KeyError as e:
//   # let this exception block's index be I
//   # write r15 to e (local variable), clear r15
//   # remove (N - I) exception blocks (note that unwind_exception already
//   #     removed the block for this clause, but there's an extra block for
//   #     the finally clause. we manually jump to the finally block so we
//   #     don't need that)
//   print('caught KeyError')
//   # if there's a finally block, jump there
//   # jump to end of suite chain
// else:
//   print('did not catch KeyError')
//   # if there's a finally block, jump there
//   # jump to end of suite chain
// finally:
//   print('executed finally block')
//   # if r15 is nonzero, call unwind_exception again

// struct ExceptionBlock {
//   ExceptionBlock* next;
//   const void* resume_rip;
//   void* resume_rsp; // stack pointer to start except block with
//   void* resume_rbp; // frame pointer to start except block with
//   int64_t num_classes;
//   int64_t exc_class_ids[0];
// };

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
