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
#include "../Compiler/Contexts.hh"
#include "../Compiler/BuiltinFunctions.hh"
#include "../Compiler/CommonObjects.hh"
#include "../Types/Strings.hh"

using namespace std;
using FragDef = BuiltinFragmentDefinition;



extern shared_ptr<GlobalContext> global;

static wstring __doc__ = L"Built-in objects specific to nemesys.";

static map<string, Value> globals({
  {"__doc__",  Value(ValueType::Unicode, __doc__)},
  {"__name__", Value(ValueType::Unicode, L"__nemesys__")},
});



static std::shared_ptr<ModuleContext> get_module(UnicodeObject* module_name) {
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



static const UnicodeObject* module_phase(ModuleContext* module) {
  if (!module) {
    return global->get_or_create_constant(L"Missing");
  }
  switch (module->phase) {
    case ModuleContext::Phase::Initial:
      return global->get_or_create_constant(L"Initial");
    case ModuleContext::Phase::Parsed:
      return global->get_or_create_constant(L"Parsed");
    case ModuleContext::Phase::Annotated:
      return global->get_or_create_constant(L"Annotated");
    case ModuleContext::Phase::Analyzed:
      return global->get_or_create_constant(L"Analyzed");
    case ModuleContext::Phase::Imported:
      return global->get_or_create_constant(L"Imported");
    default:
      return global->get_or_create_constant(L"Unknown");
  }
}

static BytesObject* module_source(ModuleContext* module) {
  if (!module) {
    return bytes_new(NULL, 0);
  }
  if (!module->source.get()) {
    return bytes_new(NULL, 0);
  }
  const string& data = module->source->data();
  return bytes_new(data.data(), data.size());
}


shared_ptr<ModuleContext> __nemesys___initialize(GlobalContext* global_context) {
  Value None(ValueType::None);
  Value Bool(ValueType::Bool);
  Value Int(ValueType::Int);
  Value Bytes(ValueType::Bytes);
  Value Unicode(ValueType::Unicode);
  Value Function(ValueType::Function);
  Value Module(ValueType::Module);

  vector<BuiltinFunctionDefinition> module_function_defs({

    {"get_module", {Unicode}, Module, void_fn_ptr([](UnicodeObject* module_name) -> ModuleContext* {
      return get_module(module_name).get();
    }), false},

    {"module_phase", {FragDef({Unicode}, Unicode, void_fn_ptr([](UnicodeObject* module_name) {
      auto module = get_module(module_name);
      delete_reference(module_name);
      return module_phase(module.get());

    })), FragDef({Module}, Unicode, void_fn_ptr(module_phase))}, false},

    {"module_compiled_size", {FragDef({Unicode}, Int, void_fn_ptr([](UnicodeObject* module_name) -> int64_t {
      auto module = get_module(module_name);
      delete_reference(module_name);
      return module.get() ? module->compiled_size : -1;

    })), FragDef({Module}, Int, void_fn_ptr([](ModuleContext* module) -> int64_t {
      return module ? module->compiled_size : -1;

    }))}, false},

    {"module_global_count", {FragDef({Unicode}, Int, void_fn_ptr([](UnicodeObject* module_name) -> int64_t {
      auto module = get_module(module_name);
      delete_reference(module_name);
      return module.get() ? module->global_variables.size() : -1;

    })), FragDef({Module}, Int, void_fn_ptr([](ModuleContext* module) -> int64_t {
      return module ? module->global_variables.size() : -1;

    }))}, false},

    {"module_source", {FragDef({Unicode}, Bytes, void_fn_ptr([](UnicodeObject* module_name) {
      auto module = get_module(module_name);
      delete_reference(module_name);
      return module_source(module.get());

    })), FragDef({Module}, Bytes, void_fn_ptr(module_source))}, false},

    {"function_id", {Function}, Int, void_fn_ptr([](FunctionContext* fn) -> int64_t {
      if (!fn) {
        return 0;
      }
      return fn->id;
    }), false},

    {"function_class_id", {Function}, Int, void_fn_ptr([](FunctionContext* fn) -> int64_t {
      if (!fn) {
        return 0;
      }
      return fn->class_id;
    }), false},

    {"function_fragment_count", {Function}, Int, void_fn_ptr([](FunctionContext* fn) -> int64_t {
      if (!fn) {
        return -1;
      }
      return fn->fragments.size();
    }), false},

    {"function_split_count", {Function}, Int, void_fn_ptr([](FunctionContext* fn) -> int64_t {
      if (!fn) {
        return -1;
      }
      return fn->num_splits;
    }), false},

    {"function_pass_exception_block", {Function}, Bool, void_fn_ptr([](FunctionContext* fn) -> bool {
      if (!fn) {
        return false;
      }
      return fn->pass_exception_block;
    }), false},

    {"code_buffer_size", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->code.total_size();
    }), false},

    {"code_buffer_used_size", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->code.total_used_bytes();
    }), false},

    {"bytes_constant_count", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->bytes_constants.size();
    }), false},

    {"unicode_constant_count", {}, Int, void_fn_ptr([]() -> int64_t {
      return global->unicode_constants.size();
    }), false},

    {"debug_flags", {}, Int, void_fn_ptr([]() -> int64_t {
      return debug_flags;
    }), false},

    {"set_debug_flags", {Int}, None, void_fn_ptr([](int64_t new_debug_flags) {
      debug_flags = new_debug_flags;
    }), false},

    {"common_object_count", {}, Int, void_fn_ptr([]() -> int64_t {
      return common_object_count();
    }), false},

    {"errno", {}, Int, void_fn_ptr([]() -> int64_t {
      return errno;
    }), false},
  });

  std::shared_ptr<ModuleContext> module(new ModuleContext(
      global_context, "__nemesys__", globals));

  for (auto& def : module_function_defs) {
    module->create_builtin_function(def);
  }

  // add debug flags as constants
  for (const auto& it : name_to_debug_flag) {
    module->create_global_variable("DebugFlag_" + it.first,
        Value(ValueType::Int, static_cast<int64_t>(it.second)), false);
  }
  return module;
}
