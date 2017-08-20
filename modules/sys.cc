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

using namespace std;



static string __doc__ = "Common built-in objects and functions.\n\
\n\
Attributes that are present have the same meaning as in Python 3.";

static string copyright = "TODO";

static map<string, Variable> globals({
  {"__doc__",              Variable(__doc__)},
  {"__name__",             Variable(L"sys")},
  {"__package__",          Variable(ValueType::None)},
  {"__stderr__",           Variable()},
  {"__stdin__",            Variable()},
  {"__stdout__",           Variable()},
  {"abiflags",             Variable(L"")},
  {"api_version",          Variable(static_cast<int64_t>(0))},
  {"argv",                 Variable()},
  {"base_exec_prefix",     Variable()},
  {"base_prefix",          Variable()},
  {"builtin_module_names", Variable()},
  {"byteorder",            Variable(L"little")},
  {"copyright",            Variable(copyright)},
  {"dont_write_bytecode",  Variable(true)},
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
  {"maxsize",              Variable(0x7FFFFFFFFFFFFFFFLL)},
  {"maxunicode",           Variable(0x10FFFFLL)},
  {"modules",              Variable()},
  {"path",                 Variable()},
  {"platform",             Variable()},
  {"prefix",               Variable()},
  {"ps1",                  Variable(L"> ")},
  {"ps2",                  Variable(L". ")},
  {"stderr",               Variable()},
  {"stdin",                Variable()},
  {"stdout",               Variable()},
  {"thread_info",          Variable()},
  {"version",              Variable(L"nemesys")},
  {"version_info",         Variable()},
});

std::shared_ptr<ModuleAnalysis> sys_module(new ModuleAnalysis("sys", globals));
