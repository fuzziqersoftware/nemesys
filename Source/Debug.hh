#pragma once

#include <inttypes.h>

#include <unordered_map>
#include <string>


enum DebugFlag {
  // printing flags are the low 16 bits; the rest are behavioral flags
  ShowSearchDebug     = 0x0000000000000001,
  ShowSourceDebug     = 0x0000000000000002,
  ShowLexDebug        = 0x0000000000000004,
  ShowParseDebug      = 0x0000000000000008,
  ShowAnnotateDebug   = 0x0000000000000010,
  ShowAnalyzeDebug    = 0x0000000000000020,
  ShowCompileDebug    = 0x0000000000000040,
  ShowAssembly        = 0x0000000000000080,
  ShowCodeSoFar       = 0x0000000000000100,
  ShowRefcountChanges = 0x0000000000000200,
  ShowJITEvents       = 0x0000000000000400,
  ShowCompileErrors   = 0x0000000000000800,
  NoInlineRefcounting = 0x0000000000010000,
  NoEagerCompilation  = 0x0000000000020000,

  Code                = 0x0000000000000CF0, // transformation steps only
  Verbose             = 0x000000000000FFFF, // no behaviors, all debug info
  All                 = 0xFFFFFFFFFFFFFFFF, // all behaviors and debug info
};

DebugFlag debug_flag_for_name(const char* name);

extern const std::unordered_map<std::string, DebugFlag> name_to_debug_flag;

extern int64_t debug_flags;
