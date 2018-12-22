#include "Debug.hh"

#include <inttypes.h>
#include <string.h>


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
  if (!strcasecmp(name, "NoInlineRefcounting")) {
    return DebugFlag::NoInlineRefcounting;
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

int64_t debug_flags = 0;
