#include "__nemesys__.hh"

#include <inttypes.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Analysis.hh"
#include "../BuiltinFunctions.hh"
#include "../Types/Strings.hh"

using namespace std;



static shared_ptr<GlobalAnalysis> global;

static wstring __doc__ = L"Built-in objects specific to nemesys.";

static map<string, Variable> globals({
  {"__doc__",              Variable(ValueType::Unicode, __doc__)},
  {"__name__",             Variable(ValueType::Unicode, L"__nemesys__")},
});

std::shared_ptr<ModuleAnalysis> __nemesys___module(new ModuleAnalysis("__nemesys__", globals));



static int64_t code_buffer_size() {
  return global->code.total_size();
}

static int64_t code_buffer_used_size() {
  return global->code.total_used_bytes();
}

static int64_t global_space() {
  return global->global_space_used;
}

static int64_t bytes_constant_count() {
  return global->bytes_constants.size();
}

static int64_t unicode_constant_count() {
  return global->unicode_constants.size();
}

static int64_t debug_flags() {
  return global->debug_flags;
}

static std::shared_ptr<ModuleAnalysis> get_module(BytesObject* module_name) {
  string module_name_str = bytes_to_cxx_string(module_name);
  try {
    return global->modules.at(module_name_str);
  } catch (const out_of_range&) {
    return NULL;
  }
}

static int64_t module_phase(BytesObject* module_name) {
  auto module = get_module(module_name);
  return module.get() ? module->phase : -1;
}

static int64_t module_compiled_size(BytesObject* module_name) {
  auto module = get_module(module_name);
  return module.get() ? module->compiled_size : -1;
}



void __nemesys___set_global(shared_ptr<GlobalAnalysis> new_global) {
  global = new_global;
}

void __nemesys___initialize() {
  // TODO: make it so we don't have to modify two maps for each global, perhaps
  // by converting globals_mutable to an unordered_set
  __nemesys___module->globals.emplace("module_phase", Variable(ValueType::Function,
      create_builtin_function("module_phase", {Variable(ValueType::Bytes)},
        Variable(ValueType::Int), reinterpret_cast<const void*>(module_phase), false)));
  __nemesys___module->globals_mutable.emplace("module_phase", false);

  __nemesys___module->globals.emplace("module_compiled_size", Variable(ValueType::Function,
      create_builtin_function("module_compiled_size", {Variable(ValueType::Bytes)},
        Variable(ValueType::Int), reinterpret_cast<const void*>(module_compiled_size), false)));
  __nemesys___module->globals_mutable.emplace("module_compiled_size", false);

  __nemesys___module->globals.emplace("code_buffer_size", Variable(ValueType::Function,
      create_builtin_function("code_buffer_size", {}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(code_buffer_size), false)));
  __nemesys___module->globals_mutable.emplace("code_buffer_size", false);

  __nemesys___module->globals.emplace("code_buffer_used_size", Variable(ValueType::Function,
      create_builtin_function("code_buffer_used_size", {}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(code_buffer_used_size), false)));
  __nemesys___module->globals_mutable.emplace("code_buffer_used_size", false);

  __nemesys___module->globals.emplace("global_space", Variable(ValueType::Function,
      create_builtin_function("global_space", {}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(global_space), false)));
  __nemesys___module->globals_mutable.emplace("global_space", false);

  __nemesys___module->globals.emplace("bytes_constant_count", Variable(ValueType::Function,
      create_builtin_function("bytes_constant_count", {}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(bytes_constant_count), false)));
  __nemesys___module->globals_mutable.emplace("bytes_constant_count", false);

  __nemesys___module->globals.emplace("unicode_constant_count", Variable(ValueType::Function,
      create_builtin_function("unicode_constant_count", {}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(unicode_constant_count), false)));
  __nemesys___module->globals_mutable.emplace("unicode_constant_count", false);

  __nemesys___module->globals.emplace("debug_flags", Variable(ValueType::Function,
      create_builtin_function("debug_flags", {}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(debug_flags), false)));
  __nemesys___module->globals_mutable.emplace("debug_flags", false);
}
