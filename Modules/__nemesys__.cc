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
#include "../CommonObjects.hh"
#include "../Types/Strings.hh"

using namespace std;
using FragDef = BuiltinFragmentDefinition;



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
  Variable None(ValueType::None);
  Variable Int(ValueType::Int);
  Variable Bytes(ValueType::Bytes);
  Variable Unicode(ValueType::Unicode);

  vector<BuiltinFunctionDefinition> module_function_defs({
    {"module_phase", {Unicode}, Int, void_fn_ptr([](UnicodeObject* module_name) -> int64_t {
      auto module = get_module(module_name);
      delete_reference(module_name);
      return module.get() ? module->phase : -1;
    }), false, false},

    {"module_compiled_size", {Unicode}, Int, void_fn_ptr([](UnicodeObject* module_name) -> int64_t {
      auto module = get_module(module_name);
      delete_reference(module_name);
      return module.get() ? module->compiled_size : -1;
    }), false, false},

    {"module_global_base", {Unicode}, Int, void_fn_ptr([](UnicodeObject* module_name) -> int64_t {
      auto module = get_module(module_name);
      delete_reference(module_name);
      return module.get() ? module->global_base_offset : -1;
    }), false, false},

    {"module_global_count", {Unicode}, Int, void_fn_ptr([](UnicodeObject* module_name) -> int64_t {
      auto module = get_module(module_name);
      delete_reference(module_name);
      return module.get() ? module->globals.size() : -1;
    }), false, false},

    {"module_source", {Unicode}, Bytes, void_fn_ptr([](UnicodeObject* module_name) -> BytesObject* {
      auto module = get_module(module_name);
      delete_reference(module_name);
      if (!module.get()) {
        return bytes_new(NULL, NULL, 0);
      }
      if (!module->source.get()) {
        return bytes_new(NULL, NULL, 0);
      }
      const string& data = module->source->data();
      return bytes_new(NULL, data.data(), data.size());
    }), false, false},

    {"code_buffer_size", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->code.total_size();
    }), false, false},

    {"code_buffer_used_size", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->code.total_used_bytes();
    }), false, false},

    {"global_space", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->global_space_used;
    }), false, false},

    {"bytes_constant_count", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->bytes_constants.size();
    }), false, false},

    {"unicode_constant_count", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->unicode_constants.size();
    }), false, false},

    {"debug_flags", {}, Int, void_fn_ptr([]() -> int64_t {
      return debug_flags;
    }), false, false},

    {"set_debug_flags", {Int}, None, void_fn_ptr([](int64_t new_debug_flags) {
      debug_flags = new_debug_flags;
    }), false, false},

    {"common_object_count", {}, Int, void_fn_ptr([]() -> int64_t {
      return common_object_count();
    }), false, false},

    {"errno", {}, Int, void_fn_ptr([]() -> int64_t {
      return errno;
    }), false, false},
  });

  for (auto& def : module_function_defs) {
    __nemesys___module->create_builtin_function(def);
  }
}
