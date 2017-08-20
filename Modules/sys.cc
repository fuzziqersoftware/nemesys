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
#include "../Types/Strings.hh"
#include "../Types/List.hh"

using namespace std;



extern shared_ptr<GlobalAnalysis> global;

static wstring __doc__ = L"Common built-in objects and functions.\n\
\n\
Attributes that are present have the same meaning as in Python 3.";

static wstring copyright = L"TODO";

static map<string, Variable> globals({
  {"__doc__",              Variable(ValueType::Unicode, __doc__)},
  {"__name__",             Variable(ValueType::Unicode, L"sys")},
  {"__package__",          Variable(ValueType::None)},
  {"__stderr__",           Variable()},
  {"__stdin__",            Variable()},
  {"__stdout__",           Variable()},
  {"abiflags",             Variable(ValueType::Unicode, L"")},
  {"api_version",          Variable(ValueType::Int, 0LL)},
  {"argv",                 Variable()},
  {"base_exec_prefix",     Variable()},
  {"base_prefix",          Variable()},
  {"builtin_module_names", Variable()},
  {"byteorder",            Variable(ValueType::Unicode, L"little")},
  {"copyright",            Variable(ValueType::Unicode, copyright)},
  {"dont_write_bytecode",  Variable(ValueType::Bool, true)},
  {"exc_info",             Variable()},
  {"exec_prefix",          Variable()},
  {"executable",           Variable()},
  {"exit",                 Variable()},
  {"flags",                Variable()},
  {"float_info",           Variable()},
  {"float_repr_style",     Variable()},
  {"getrecursionlimit",    Variable()},
  {"getrefcount",          Variable()},
  {"getsizeof",            Variable()},
  {"hash_info",            Variable()},
  {"hexversion",           Variable()},
  {"implementation",       Variable()},
  {"int_info",             Variable()},
  {"last_traceback",       Variable()},
  {"last_type",            Variable()},
  {"last_value",           Variable()},
  {"maxsize",              Variable(ValueType::Int, 0x7FFFFFFFFFFFFFFFLL)},
  {"maxunicode",           Variable(ValueType::Int, 0x10FFFFLL)},
  {"modules",              Variable()},
  {"path",                 Variable()},
  {"platform",             Variable()},
  {"prefix",               Variable()},
  {"ps1",                  Variable(ValueType::Unicode, L"> ")},
  {"ps2",                  Variable(ValueType::Unicode, L". ")},
  {"stderr",               Variable()},
  {"stdin",                Variable()},
  {"stdout",               Variable()},
  {"thread_info",          Variable()},
  {"version",              Variable(ValueType::Unicode, L"nemesys")},
  {"version_info",         Variable()},
});

std::shared_ptr<ModuleAnalysis> sys_module(new ModuleAnalysis("sys", globals));

void sys_set_argv(const vector<const char*>& sys_argv) {
  vector<shared_ptr<Variable>> argv;
  for (const char* arg : sys_argv) {
    argv.emplace_back(new Variable(ValueType::Bytes,
        reinterpret_cast<const uint8_t*>(arg)));
  }
  sys_module->globals.emplace("argv", Variable(ValueType::List, argv));
}

void sys_initialize() {
  // nothing to do here for now. in the future this is where we would create
  // function ids for the functions defined in this module
}
