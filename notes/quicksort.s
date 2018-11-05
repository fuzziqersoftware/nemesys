.intel_syntax noprefix

# quicksort implementation. this function sorts an array of 64-bit integers.
# this follows the system v calling convention (sort of). call it with the array
# pointer in rdi and the number of elements in the array in rsi. it doesn't
# return anything; rax is meaningless after calling it.

# in c: void quicksort(int64_t* items, size_t count);

.globl _quicksort
_quicksort:
  # we use rdx as the array ptr everywhere (in recursive calls too). in the rest
  # of the implementation, rdi is the start index and rsi is the end index
  mov   rdx, rdi

  # the first recursive "call" uses the last value as the pivot, so it starts
  # with rdi = 0, rsi = count - 1
  xor   rdi, rdi
  dec   rsi

_quicksort__recursive_call:
  # first check if start < end; if not, return without doing anything
  cmp   rdi, rsi
  jl    _quicksort__dont_return
  ret
_quicksort__dont_return:

  # compute pivot_idx = (start + end) / 2 in rcx
  lea   rcx, [rdi + rsi]
  shr   rcx, 1

  # move the pivot to the end, leaving pivot_value in rax (will need it later)
  mov   rax, [rdx + rsi * 8]
  xchg  rax, [rdx + rcx * 8]
  mov   [rdx + rsi * 8], rax

  # now we loop over the values and separate them into low and high buckets. r8
  # will point to the next value to be bucketed, and r9 will point to the index
  # between the two buckets. start with r8 = r9 = start, and decrement r8 first
  # so we can increment it at the beginning of the loop.
  lea   r8, [rdi - 1]
  mov   r9, rdi
_quicksort__continue_bucketing:

  # increment r8, then end the loop if r8 >= end (all values are bucketed)
  inc   r8
  cmp   r8, rsi
  jge   _quicksort__done_bucketing

  # if a[r8] >= pivot_value, leave this value in the high bucket (increment r8)
  cmp   [rdx + r8 * 8], rax
  jge   _quicksort__continue_bucketing

  # a[r8] < pivot_value, so swap a[r8] and a[r9] and increment r9. this moves
  # a[r9] into the low bucket, and moves the first element in the high bucket to
  # the end of the high bucket.
  mov   rcx, [rdx + r9 * 8]
  xchg  rcx, [rdx + r8 * 8]
  mov   [rdx + r9 * 8], rcx
  inc   r9
  jmp   _quicksort__continue_bucketing
_quicksort__done_bucketing:

  # done bucketing. swap a[end] and a[j] to put the pivot value between the
  # buckets, so we can recur on both buckets. (rax is still pivot_value)
  xchg  rax, [rdx + r9 * 8]
  mov   [rdx + rsi * 8], rax

  # the second recursive call will be quicksort(pivot_idx + 1, end). push these
  # args onto the stack since we can't recompute them after the first call
  push  rsi
  lea   rax, [r9 + 1]
  push  rax

  # recur on the first half by calling quicksort(start, pivot_idx - 1)
  lea   rsi, [r9 - 1]
  call  _quicksort__recursive_call

  # now recur on the second half
  pop   rdi
  pop   rsi
  jmp   _quicksort__recursive_call
