import __nemesys__

phase = __nemesys__.module_phase(__name__)
compiled_size = __nemesys__.module_compiled_size(__name__)
global_base = __nemesys__.module_global_base(__name__)
global_count = __nemesys__.module_global_count(__name__)

code_buffer_size = __nemesys__.code_buffer_size()
code_buffer_used_size = __nemesys__.code_buffer_used_size()

global_space = __nemesys__.global_space()



print('''nemesys counters:

phase(%s) == %s
compiled_size(%s) == 0x%X
global_base(%s) == 0x%X
global_count(%s) == 0x%X

code_buffer_size == 0x%X
code_buffer_used_size == 0x%X

global_space == 0x%X''' % (
    repr(__name__), phase,
    repr(__name__), compiled_size,
    repr(__name__), global_base,
    repr(__name__), global_count,
    code_buffer_size,
    code_buffer_used_size,
    global_space))



assert phase == "Analyzed"  # doesn't become Imported until the root scope returns
assert compiled_size > 0
assert global_base >= 0
assert global_count == 10
# note: the globals are __doc__, __name__, __nemesys__, and the 7 vars above

assert code_buffer_size > 0
assert code_buffer_used_size > 0

assert global_space > 0
