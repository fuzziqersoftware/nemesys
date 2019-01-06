.intel_syntax noprefix

# function call resolution entry point when called from nemesys code. note that
# this function does not follow the system v calling convention that we adhere
# to in the rest of this project! the arguments to the function being compiled
# are in the normal registers, but r10 specifies the global context pointer and
# r11 specifies the callsite token (which the compiler uses to look up the
# appropriate module context, function id, argument types, etc.)
.globl _resolve_function_call
.globl __resolve_function_call
_resolve_function_call:
__resolve_function_call:

  # TODO: we can avoid wasting time and stack space here by passing in the
  # number of arguments along with the callsite token, but I'm lazy

  # save the int arguments on the stack
  push r9
  push r8
  push rcx
  push rdx
  push rsi
  push rdi
  mov rdx, rsp  # _jit_compile_scope will want this later, so save it now

  mov rcx, rsp
  and rcx, 15
  cmp rcx, 8
  je stack_ok
  mov byte ptr [0], 0
stack_ok:

  # save the float arguments on the stack
  movq rdi, xmm7
  push rdi
  movq rdi, xmm6
  push rdi
  movq rdi, xmm5
  push rdi
  movq rdi, xmm4
  push rdi
  movq rdi, xmm3
  push rdi
  movq rdi, xmm2
  push rdi
  movq rdi, xmm1
  push rdi
  movq rdi, xmm0
  push rdi

  # because we pushed an even number of items, the stack is still misaligned for
  # calling into C++ code, so we align it manually here. we'll also use this
  # space for _jit_compile_scope's second return value (the exception object)
  sub rsp, 8
  mov rcx, rsp

  # the first argument is the callsite token (the others are already set up)
  mov rdi, r10
  mov rsi, r11

  # on success, this function returns the address of the newly-compiled fragment
  # and sets the address pointed to by rdx to the new return address. on
  # failure, it returns NULL and sets the address pointed to by rdx to the
  # address of a NemesysCompilerError object, which should be raised to the
  # calling code. in the failure case, this function also releases the argument
  # references appropriately.
  call _jit_compile_scope

  # on failure, don't restore the args; just raise the exception by putting it
  # in r15
  test rax, rax
  jnz return_no_exception
  pop r15  # get the exception object
  add rsp, 112  # eliminate the space for the saved regs
  ret

return_no_exception:
  # compilation succeeded! this means that the exception return value is unused,
  # so ignore it
  add rsp, 8

  # restore the argument registers
  pop rdi
  movq xmm0, rdi
  pop rdi
  movq xmm1, rdi
  pop rdi
  movq xmm2, rdi
  pop rdi
  movq xmm3, rdi
  pop rdi
  movq xmm4, rdi
  pop rdi
  movq xmm5, rdi
  pop rdi
  movq xmm6, rdi
  pop rdi
  movq xmm7, rdi

  pop rdi
  pop rsi
  pop rdx
  pop rcx
  pop r8
  pop r9

  # _jit_compile_scope returned the address of the opcode that calls the
  # necessary callee fragment in the new caller fragment. so all we have to do
  # is go there
  add rsp, 8
  jmp rax
