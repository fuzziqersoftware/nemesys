#include "Debug.hh"

#include <inttypes.h>
#include <string.h>

using namespace std;


DebugFlag debug_flag_for_name(const char* name) {
  if (!strcasecmp(name, "ShowSearchDebug")) {
    return DebugFlag::ShowSearchDebug;
  }
  if (!strcasecmp(name, "ShowSourceDebug")) {
    return DebugFlag::ShowSourceDebug;
  }
  if (!strcasecmp(name, "ShowLexDebug")) {
    return DebugFlag::ShowLexDebug;
  }
  if (!strcasecmp(name, "ShowParseDebug")) {
    return DebugFlag::ShowParseDebug;
  }
  if (!strcasecmp(name, "ShowAnnotateDebug")) {
    return DebugFlag::ShowAnnotateDebug;
  }
  if (!strcasecmp(name, "ShowAnalyzeDebug")) {
    return DebugFlag::ShowAnalyzeDebug;
  }
  if (!strcasecmp(name, "ShowCompileDebug")) {
    return DebugFlag::ShowCompileDebug;
  }
  if (!strcasecmp(name, "ShowAssembly")) {
    return DebugFlag::ShowAssembly;
  }
  if (!strcasecmp(name, "ShowRefcountChanges")) {
    return DebugFlag::ShowRefcountChanges;
  }
  if (!strcasecmp(name, "ShowJITEvents")) {
    return DebugFlag::ShowJITEvents;
  }
  if (!strcasecmp(name, "NoInlineRefcounting")) {
    return DebugFlag::NoInlineRefcounting;
  }
  if (!strcasecmp(name, "NoEagerCompilation")) {
    return DebugFlag::NoEagerCompilation;
  }
  if (!strcasecmp(name, "Code")) {
    return DebugFlag::Code;
  }
  if (!strcasecmp(name, "Verbose")) {
    return DebugFlag::Verbose;
  }
  if (!strcasecmp(name, "All")) {
    return DebugFlag::All;
  }
  return static_cast<DebugFlag>(0);
}


const unordered_map<string, DebugFlag> name_to_debug_flag({
  {"ShowSearchDebug"    , DebugFlag::ShowSearchDebug},
  {"ShowSourceDebug"    , DebugFlag::ShowSourceDebug},
  {"ShowLexDebug"       , DebugFlag::ShowLexDebug},
  {"ShowParseDebug"     , DebugFlag::ShowParseDebug},
  {"ShowAnnotateDebug"  , DebugFlag::ShowAnnotateDebug},
  {"ShowAnalyzeDebug"   , DebugFlag::ShowAnalyzeDebug},
  {"ShowCompileDebug"   , DebugFlag::ShowCompileDebug},
  {"ShowAssembly"       , DebugFlag::ShowAssembly},
  {"ShowCodeSoFar"      , DebugFlag::ShowCodeSoFar},
  {"ShowRefcountChanges", DebugFlag::ShowRefcountChanges},
  {"ShowJITEvents"      , DebugFlag::ShowJITEvents},
  {"ShowCompileErrors"  , DebugFlag::ShowCompileErrors},
  {"NoInlineRefcounting", DebugFlag::NoInlineRefcounting},
  {"NoEagerCompilation" , DebugFlag::NoEagerCompilation},
  {"Code"               , DebugFlag::Code},
  {"Verbose"            , DebugFlag::Verbose},
  {"All"                , DebugFlag::All},
});

int64_t debug_flags = 0;
