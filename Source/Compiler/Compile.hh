#pragma once

#include <memory>
#include <unordered_map>
#include <string>

#include "Contexts.hh"
#include "../Environment/Value.hh"



void advance_module_phase(GlobalContext* global, ModuleContext* module,
    ModuleContext::Phase phase);

void compile_fragment(GlobalContext* global, ModuleContext* module, Fragment* f);

void initialize_global_space_for_module(GlobalContext* global,
    ModuleContext* module);


extern "C" {

const void* jit_compile_scope(GlobalContext* global, int64_t callsite_token,
    uint64_t* int_args, void** return_address_or_exception);

// everything below here is implemented in Exception-Assembly.s

// compile a function scope from within nemesys-generated code. this function
// cannot be called normally; it can only be called from code generated by
// nemesys because it accepts arguments in nonstandard registers. to compile a
// scope from C++ code, use compile_scope above.
void _resolve_function_call();

} // extern "C"
