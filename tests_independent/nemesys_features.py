import __nemesys__

import posix


def check_counters():
  code_buffer_size = __nemesys__.code_buffer_size()
  code_buffer_used_size = __nemesys__.code_buffer_used_size()

  print('''nemesys counters:
code_buffer_size == 0x%X
code_buffer_used_size == 0x%X''' % (
      code_buffer_size,
      code_buffer_used_size))

  assert code_buffer_size > 0
  assert code_buffer_used_size > 0

check_counters()


def check_eager_compilation():
  # if eager compilation is enabled, the code size should not increase across this
  # function call

  f = lambda a, b, c: a * b + c

  print('f has %d fragments' % __nemesys__.function_fragment_count(f))

  pre_call_size = __nemesys__.code_buffer_used_size()
  assert f(3, 4, 5) == 17, "wtf how did any of the other tests pass"
  post_call_size = __nemesys__.code_buffer_used_size()

  print('code buffer size before call: %d' % pre_call_size)
  print('code buffer size after call: %d' % post_call_size)

  if __nemesys__.debug_flags() & __nemesys__.DebugFlag_NoEagerCompilation:
    print('note: eager compilation is disabled')
    assert pre_call_size != post_call_size
  else:
    print('note: eager compilation is enabled')
    assert pre_call_size == post_call_size

check_eager_compilation()


def check_function_inspection():
  id1 = __nemesys__.function_id(check_counters)
  id2 = __nemesys__.function_id(check_eager_compilation)
  id3 = __nemesys__.function_id(check_function_inspection)
  assert (id1 > 0) and (id2 > 0) and (id3 > 0), "some user-defined functions had nonpositive IDs"
  assert (id1 != id2) and (id2 != id3) and (id3 != id1), "some user-defined functions had equal IDs"

  assert __nemesys__.function_class_id(check_counters) == 0, "check_counters had a nonzero class_id"
  assert __nemesys__.function_class_id(check_eager_compilation) == 0, "check_eager_compilation had a nonzero class_id"
  assert __nemesys__.function_class_id(check_function_inspection) == 0, "check_function_inspection had a nonzero class_id"

  assert __nemesys__.function_fragment_count(check_counters) == 1, "check_counters didn\'t have exactly one fragment"
  assert __nemesys__.function_fragment_count(check_eager_compilation) == 1, "check_eager_compilation didn\'t have exactly one fragment"
  assert __nemesys__.function_fragment_count(check_function_inspection) == 1, "check_function_inspection didn\'t have exactly one fragment"

  assert __nemesys__.function_split_count(check_counters) >= 0, "check_counters had an invalid split count"
  assert __nemesys__.function_split_count(check_eager_compilation) >= 0, "check_eager_compilation had an invalid split count"
  assert __nemesys__.function_split_count(check_function_inspection) >= 0, "check_function_inspection had an invalid split count"

  # note: this returns whether the exception block is EXPLICITLY passed into the
  # function. user-defined functions take it implicitly, so these are all False
  assert not __nemesys__.function_pass_exception_block(check_counters), "check_counters did not take an exception block"
  assert not __nemesys__.function_pass_exception_block(check_eager_compilation), "check_eager_compilation did not take an exception block"
  assert not __nemesys__.function_pass_exception_block(check_function_inspection), "check_function_inspection did not take an exception block"

check_function_inspection()


def check_module_inspection():
  this_module = __nemesys__.get_module(__name__)

  phase = __nemesys__.module_phase(__name__)
  compiled_size = __nemesys__.module_compiled_size(__name__)
  global_count = __nemesys__.module_global_count(__name__)
  source = __nemesys__.module_source(__name__)

  # these functions should also work if passed the module object
  assert phase == __nemesys__.module_phase(this_module)
  assert compiled_size == __nemesys__.module_compiled_size(this_module)
  assert global_count == __nemesys__.module_global_count(this_module)
  assert source == __nemesys__.module_source(this_module)

  assert phase == "Analyzed"  # doesn't become Imported until the root scope returns
  assert compiled_size > 0
  assert global_count == 8
  # note: the globals are __doc__, __name__, __nemesys__, posix, and the four functions

  assert b'this string appears verbatim in the module source' in source
  assert b'this string does not appear verbatim because it has an escaped\x20character' not in source

  # read the contents of this file and make sure it matches module_source
  fd = posix.open(__file__, posix.O_RDONLY)
  data = posix.read(fd, 2 * len(source))
  posix.close(fd)
  assert data == source, '%s != %s' % (repr(data), repr(source))

check_module_inspection()
