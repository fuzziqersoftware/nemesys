#include "__nemesys__.hh"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <phosg/Time.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Debug.hh"
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



void __nemesys___initialize() {
  Variable Int(ValueType::Int);
  Variable Bytes(ValueType::Bytes);
  Variable Unicode(ValueType::Unicode);

  __nemesys___module->create_builtin_function("module_phase", {Unicode}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* module_name) -> int64_t {
    auto module = get_module(module_name);
    return module.get() ? module->phase : -1;
  }));

  __nemesys___module->create_builtin_function("module_compiled_size", {Unicode}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* module_name) -> int64_t {
    auto module = get_module(module_name);
    return module.get() ? module->compiled_size : -1;
  }));

  __nemesys___module->create_builtin_function("module_global_base", {Unicode}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* module_name) -> int64_t {
    auto module = get_module(module_name);
    return module.get() ? module->global_base_offset : -1;
  }));

  __nemesys___module->create_builtin_function("module_global_count", {Unicode}, Int,
      reinterpret_cast<const void*>(+[](UnicodeObject* module_name) -> int64_t {
    auto module = get_module(module_name);
    return module.get() ? module->globals.size() : -1;
  }));

  __nemesys___module->create_builtin_function("module_source", {Unicode}, Bytes,
      reinterpret_cast<const void*>(+[](UnicodeObject* module_name) -> BytesObject* {
    auto module = get_module(module_name);
    if (!module.get()) {
      return bytes_new(NULL, NULL, 0);
    }
    if (!module->source.get()) {
      return bytes_new(NULL, NULL, 0);
    }
    const string& data = module->source->data();
    return bytes_new(NULL, data.data(), data.size());
  }));

  __nemesys___module->create_builtin_function("code_buffer_size", {}, Int,
      reinterpret_cast<const void*>(+[]() -> int64_t {
    return global->code.total_size();
  }));

  __nemesys___module->create_builtin_function("code_buffer_used_size", {}, Int,
      reinterpret_cast<const void*>(+[]() -> int64_t {
    return global->code.total_used_bytes();
  }));

  __nemesys___module->create_builtin_function("global_space", {}, Int,
      reinterpret_cast<const void*>(+[]() -> int64_t {
    return global->global_space_used;
  }));

  __nemesys___module->create_builtin_function("bytes_constant_count", {}, Int,
      reinterpret_cast<const void*>(+[]() -> int64_t {
    return global->bytes_constants.size();
  }));

  __nemesys___module->create_builtin_function("unicode_constant_count", {}, Int,
      reinterpret_cast<const void*>(+[]() -> int64_t {
    return global->unicode_constants.size();
  }));

  __nemesys___module->create_builtin_function("debug_flags", {}, Int,
      reinterpret_cast<const void*>(+[]() -> int64_t {
    return debug_flags;
  }));

  __nemesys___module->create_builtin_function("errno", {}, Int,
      reinterpret_cast<const void*>(+[]() -> int64_t {
    return errno;
  }));

  __nemesys___module->create_builtin_function("time", {}, Int,
      reinterpret_cast<const void*>(&now));
}
