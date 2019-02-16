#include "time.hh"

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include <memory>
#include <phosg/Time.hh>
#include <string>
#include <unordered_map>

#include "../Compiler/Contexts.hh"
#include "../Compiler/BuiltinFunctions.hh"
#include "../Environment/Value.hh"
#include "../Types/Dictionary.hh"

using namespace std;



static wstring __doc__ = L"Standard error code symbols.";

static map<string, Value> globals({
  {"__doc__",                  Value(ValueType::Unicode, __doc__)},
  {"__package__",              Value(ValueType::Unicode, L"")},

  {"CLOCK_MONOTONIC",          Value(ValueType::Int, static_cast<int64_t>(CLOCK_MONOTONIC))},
  {"CLOCK_MONOTONIC_RAW",      Value(ValueType::Int, static_cast<int64_t>(CLOCK_MONOTONIC_RAW))},
  {"CLOCK_REALTIME",           Value(ValueType::Int, static_cast<int64_t>(CLOCK_REALTIME))},
  {"CLOCK_THREAD_CPUTIME_ID",  Value(ValueType::Int, static_cast<int64_t>(CLOCK_THREAD_CPUTIME_ID))},
  {"CLOCK_PROCESS_CPUTIME_ID", Value(ValueType::Int, static_cast<int64_t>(CLOCK_PROCESS_CPUTIME_ID))},

  // unimplemented stuff:
  // _STRUCT_TM_ITEMS (osx only)
  // altzone
  // asctime
  // clock
  // clock_getres (linux only)
  // clock_gettime (linux only)
  // clock_settime (linux only)
  // ctime
  // daylight
  // get_clock_info
  // gmtime
  // localtime
  // mktime
  // monotonic
  // perf_counter
  // process_time
  // strftime
  // strptime
  // struct_time
  // timezone
  // tzname
  // tzset
});

shared_ptr<ModuleContext> time_initialize(GlobalContext* global) {
  Value None(ValueType::None);
  Value Int(ValueType::Int);
  Value Float(ValueType::Float);

  vector<BuiltinFunctionDefinition> module_function_defs({
    {"time", {}, Float, void_fn_ptr([]() -> double {
      return static_cast<double>(now()) / 1000000.0;
    }), false},

    {"utime", {}, Int, void_fn_ptr(&now), false},

    {"sleep", {Float}, None, void_fn_ptr([](double secs) {
      usleep(static_cast<int64_t>(secs * 1000000));
    }), false},
  });

  shared_ptr<ModuleContext> module(new ModuleContext(global, "time", globals));
  for (auto& def : module_function_defs) {
    module->create_builtin_function(def);
  }
  return module;
}
