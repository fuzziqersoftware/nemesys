#include "sys.hh"

#include <inttypes.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Compiler/Contexts.hh"
#include "../Compiler/BuiltinFunctions.hh"
#include "../Types/Strings.hh"
#include "../Types/List.hh"

using namespace std;



extern shared_ptr<GlobalContext> global;

static wstring __doc__ = L"Common built-in objects and functions.\n\
\n\
Attributes that are present have the same meanings as in Python 3.";

static map<string, Value> globals({
  {"__doc__",              Value(ValueType::Unicode, __doc__)},
  {"__name__",             Value(ValueType::Unicode, L"sys")},
  {"__package__",          Value(ValueType::None)},
  // {"__stderr__",           Value()},
  // {"__stdin__",            Value()},
  // {"__stdout__",           Value()},
  {"abiflags",             Value(ValueType::Unicode, L"")},
  {"api_version",          Value(ValueType::Int, static_cast<int64_t>(0))},
  // {"base_exec_prefix",     Value()},
  // {"base_prefix",          Value()},
  // {"builtin_module_names", Value()},
  {"byteorder",            Value(ValueType::Unicode, L"little")},
  {"copyright",            Value(ValueType::Unicode, L"")},
  {"dont_write_bytecode",  Value(ValueType::Bool, true)},
  // {"exc_info",             Value()},
  // {"exec_prefix",          Value()},
  // {"exit",                 Value()},
  // {"flags",                Value()},
  // {"float_info",           Value()},
  // {"float_repr_style",     Value()},
  // {"getrecursionlimit",    Value()},
  // {"getsizeof",            Value()},
  // {"hash_info",            Value()},
  {"hexversion",           Value(ValueType::Int, static_cast<int64_t>(0))},
  // {"implementation",       Value()},
  // {"int_info",             Value()},
  // {"last_traceback",       Value()},
  // {"last_type",            Value()},
  // {"last_value",           Value()},
  {"maxsize",              Value(ValueType::Int, static_cast<int64_t>(0x7FFFFFFFFFFFFFFFLL))},
  {"maxunicode",           Value(ValueType::Int, static_cast<int64_t>(0x10FFFFLL))},
  // {"modules",              Value()},
  // {"path",                 Value()},
  // {"platform",             Value()},
  // {"prefix",               Value()},
  {"ps1",                  Value(ValueType::Unicode, L"> ")},
  {"ps2",                  Value(ValueType::Unicode, L". ")},
  // {"stderr",               Value()},
  // {"stdin",                Value()},
  // {"stdout",               Value()},
  // {"thread_info",          Value()},
  {"version",              Value(ValueType::Unicode, L"nemesys")},
  // {"version_info",         Value()},
});

std::shared_ptr<ModuleContext> sys_module(new ModuleContext("sys", globals));

void sys_set_executable(const char* realpath) {
  sys_module->globals.emplace("executable", Value(ValueType::Bytes, realpath));
}

void sys_set_argv(const vector<const char*>& sys_argv) {
  vector<shared_ptr<Value>> argv;
  for (const char* arg : sys_argv) {
    wstring warg;
    for (; *arg; arg++) {
      warg += static_cast<wchar_t>(*arg);
    }
    argv.emplace_back(new Value(ValueType::Unicode, move(warg)));
  }
  sys_module->globals.emplace("argv", Value(ValueType::List, move(argv)));
}

void sys_initialize() {
  Value Indeterminate(ValueType::Indeterminate);
  Value Int(ValueType::Int);
  Value Bytes(ValueType::Bytes);
  Value Unicode(ValueType::Unicode);
  Value List(ValueType::List, vector<Value>({Indeterminate}));
  Value Tuple(ValueType::Tuple, vector<Value>({Indeterminate}));
  Value Set(ValueType::Set, vector<Value>({Indeterminate}));
  Value Dict(ValueType::Dict, vector<Value>({Indeterminate, Indeterminate}));

  static auto getrefcount = +[](BasicObject* a) -> int64_t {
    return a->refcount;
  };

  vector<BuiltinFunctionDefinition> module_function_defs({
    // TODO: this should support Instance also, but this will require some
    // typecheck hax
    {"getrefcount", {{{Bytes}, Int, void_fn_ptr(getrefcount)},
        {{Unicode}, Int, void_fn_ptr(getrefcount)},
        {{List}, Int, void_fn_ptr(getrefcount)},
        {{Tuple}, Int, void_fn_ptr(getrefcount)},
        {{Set}, Int, void_fn_ptr(getrefcount)},
        {{Dict}, Int, void_fn_ptr(getrefcount)}}, false, false},
  });

  for (auto& def : module_function_defs) {
    sys_module->create_builtin_function(def);
  }
}
