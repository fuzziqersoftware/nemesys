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

#include "../Analysis.hh"
#include "../BuiltinFunctions.hh"
#include "../Environment.hh"
#include "../Types/Dictionary.hh"

using namespace std;



extern shared_ptr<GlobalAnalysis> global;

static wstring __doc__ = L"Standard error code symbols.";

static map<string, Variable> globals({
  {"__doc__",                  Variable(ValueType::Unicode, __doc__)},
  {"__package__",              Variable(ValueType::Unicode, L"")},

  {"CLOCK_MONOTONIC",          Variable(ValueType::Int, static_cast<int64_t>(CLOCK_MONOTONIC))},
  {"CLOCK_MONOTONIC_RAW",      Variable(ValueType::Int, static_cast<int64_t>(CLOCK_MONOTONIC_RAW))},
  {"CLOCK_REALTIME",           Variable(ValueType::Int, static_cast<int64_t>(CLOCK_REALTIME))},
  {"CLOCK_THREAD_CPUTIME_ID",  Variable(ValueType::Int, static_cast<int64_t>(CLOCK_THREAD_CPUTIME_ID))},
  {"CLOCK_PROCESS_CPUTIME_ID", Variable(ValueType::Int, static_cast<int64_t>(CLOCK_PROCESS_CPUTIME_ID))},

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

std::shared_ptr<ModuleAnalysis> time_module(new ModuleAnalysis("time", globals));

void time_initialize() {
  Variable None(ValueType::None);
  Variable Int(ValueType::Int);
  Variable Float(ValueType::Float);

  vector<BuiltinFunctionDefinition> module_function_defs({
    {"time", {}, Float, void_fn_ptr([]() -> double {
      return static_cast<double>(now()) / 1000000.0;
    }), false, false},

    {"utime", {}, Int, void_fn_ptr(&now), false, false},

    {"sleep", {Float}, None, void_fn_ptr([](double secs) {
      usleep(static_cast<int64_t>(secs * 1000000));
    }), false, false},
  });

  for (auto& def : module_function_defs) {
    time_module->create_builtin_function(def);
  }
}
