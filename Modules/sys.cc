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

#include "../Analysis.hh"
#include "../BuiltinFunctions.hh"
#include "../Types/Strings.hh"
#include "../Types/List.hh"

using namespace std;



extern shared_ptr<GlobalAnalysis> global;

static wstring __doc__ = L"Common built-in objects and functions.\n\
\n\
Attributes that are present have the same meanings as in Python 3.";

static map<string, Variable> globals({
  {"__doc__",              Variable(ValueType::Unicode, __doc__)},
  {"__name__",             Variable(ValueType::Unicode, L"sys")},
  {"__package__",          Variable(ValueType::None)},
  // {"__stderr__",           Variable()},
  // {"__stdin__",            Variable()},
  // {"__stdout__",           Variable()},
  {"abiflags",             Variable(ValueType::Unicode, L"")},
  {"api_version",          Variable(ValueType::Int, 0LL)},
  // {"base_exec_prefix",     Variable()},
  // {"base_prefix",          Variable()},
  // {"builtin_module_names", Variable()},
  {"byteorder",            Variable(ValueType::Unicode, L"little")},
  {"copyright",            Variable(ValueType::Unicode, L"")},
  {"dont_write_bytecode",  Variable(ValueType::Bool, true)},
  // {"exc_info",             Variable()},
  // {"exec_prefix",          Variable()},
  // {"exit",                 Variable()},
  // {"flags",                Variable()},
  // {"float_info",           Variable()},
  // {"float_repr_style",     Variable()},
  // {"getrecursionlimit",    Variable()},
  // {"getsizeof",            Variable()},
  // {"hash_info",            Variable()},
  {"hexversion",           Variable(ValueType::Int, 0LL)},
  // {"implementation",       Variable()},
  // {"int_info",             Variable()},
  // {"last_traceback",       Variable()},
  // {"last_type",            Variable()},
  // {"last_value",           Variable()},
  {"maxsize",              Variable(ValueType::Int, 0x7FFFFFFFFFFFFFFFLL)},
  {"maxunicode",           Variable(ValueType::Int, 0x10FFFFLL)},
  // {"modules",              Variable()},
  // {"path",                 Variable()},
  // {"platform",             Variable()},
  // {"prefix",               Variable()},
  {"ps1",                  Variable(ValueType::Unicode, L"> ")},
  {"ps2",                  Variable(ValueType::Unicode, L". ")},
  // {"stderr",               Variable()},
  // {"stdin",                Variable()},
  // {"stdout",               Variable()},
  // {"thread_info",          Variable()},
  {"version",              Variable(ValueType::Unicode, L"nemesys")},
  // {"version_info",         Variable()},
});

std::shared_ptr<ModuleAnalysis> sys_module(new ModuleAnalysis("sys", globals));

void sys_set_executable(const char* realpath) {
  sys_module->globals.emplace("executable", Variable(ValueType::Bytes, realpath));
}

void sys_set_argv(const vector<const char*>& sys_argv) {
  vector<shared_ptr<Variable>> argv;
  for (const char* arg : sys_argv) {
    wstring warg;
    for (; *arg; arg++) {
      warg += static_cast<wchar_t>(*arg);
    }
    argv.emplace_back(new Variable(ValueType::Unicode, move(warg)));
  }
  sys_module->globals.emplace("argv", Variable(ValueType::List, move(argv)));
}

void sys_initialize() {
  Variable Indeterminate(ValueType::Indeterminate);
  Variable Int(ValueType::Int);
  Variable Bytes(ValueType::Bytes);
  Variable Unicode(ValueType::Unicode);
  Variable List(ValueType::List, vector<Variable>({Indeterminate}));
  Variable Tuple(ValueType::Tuple, vector<Variable>({Indeterminate}));
  Variable Set(ValueType::Set, vector<Variable>({Indeterminate}));
  Variable Dict(ValueType::Dict, vector<Variable>({Indeterminate, Indeterminate}));

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
