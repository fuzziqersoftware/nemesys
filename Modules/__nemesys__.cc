#include "__nemesys__.hh"

#include <errno.h>
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
using FragDef = FunctionContext::BuiltinFunctionFragmentDefinition;



extern shared_ptr<GlobalAnalysis> global;

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

static std::shared_ptr<ModuleAnalysis> get_module(UnicodeObject* module_name) {
  string module_name_str;
  module_name_str.reserve(module_name->count);
  for (size_t x = 0; x < module_name->count; x++) {
    module_name_str += static_cast<char>(module_name->data[x]);
  }
  try {
    return global->modules.at(module_name_str);
  } catch (const out_of_range&) {
    return NULL;
  }
}

static int64_t module_phase(UnicodeObject* module_name) {
  auto module = get_module(module_name);
  return module.get() ? module->phase : -1;
}

static int64_t module_compiled_size(UnicodeObject* module_name) {
  auto module = get_module(module_name);
  return module.get() ? module->compiled_size : -1;
}

static int64_t module_global_base(UnicodeObject* module_name) {
  auto module = get_module(module_name);
  return module.get() ? module->global_base_offset : -1;
}

static int64_t module_global_count(UnicodeObject* module_name) {
  auto module = get_module(module_name);
  return module.get() ? module->globals.size() : -1;
}

static BytesObject* module_source(UnicodeObject* module_name) {
  auto module = get_module(module_name);
  if (!module.get()) {
    return bytes_new(NULL, NULL, 0);
  }
  if (!module->source.get()) {
    return bytes_new(NULL, NULL, 0);
  }
  const string& data = module->source->data();
  return bytes_new(NULL, data.data(), data.size());
}

static int64_t get_errno() {
  return errno;
}



void __nemesys___initialize() {
  __nemesys___module->create_builtin_function("module_phase",
      {Variable(ValueType::Unicode)}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&module_phase));

  __nemesys___module->create_builtin_function("module_compiled_size",
      {Variable(ValueType::Unicode)}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&module_compiled_size));

  __nemesys___module->create_builtin_function("module_global_base",
      {Variable(ValueType::Unicode)}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&module_global_base));

  __nemesys___module->create_builtin_function("module_global_count",
      {Variable(ValueType::Unicode)}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&module_global_count));

  __nemesys___module->create_builtin_function("module_source",
      {Variable(ValueType::Unicode)}, Variable(ValueType::Bytes),
      reinterpret_cast<const void*>(&module_source));

  __nemesys___module->create_builtin_function("code_buffer_size",
      {}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&code_buffer_size));

  __nemesys___module->create_builtin_function("code_buffer_used_size",
      {}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&code_buffer_used_size));

  __nemesys___module->create_builtin_function("global_space",
      {}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&global_space));

  __nemesys___module->create_builtin_function("bytes_constant_count",
      {}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&bytes_constant_count));

  __nemesys___module->create_builtin_function("unicode_constant_count",
      {}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&unicode_constant_count));

  __nemesys___module->create_builtin_function("debug_flags",
      {}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&debug_flags));

  __nemesys___module->create_builtin_function("errno",
      {}, Variable(ValueType::Int),
      reinterpret_cast<const void*>(&get_errno));
}
