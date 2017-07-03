#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <phosg/Strings.hh>

#include "SourceFile.hh"
#include "PythonLexer.hh"
#include "PythonParser.hh"
#include "Environment.hh"
#include "Analysis.hh"

using namespace std;


int main(int argc, char* argv[]) {

  if (argc < 2) {
    printf("Usage: %s --phase=PHASE module_name [module_name ...]\n", argv[0]);
    return (-1);
  }

  GlobalAnalysis global;
  ModuleAnalysis::Phase target_phase = ModuleAnalysis::Phase::Imported;
  vector<string> module_names;
  for (size_t x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "--phase=", 8)) {
      if (!strcasecmp(&argv[x][8], "Initial")) {
        target_phase = ModuleAnalysis::Phase::Initial;
      } else if (!strcasecmp(&argv[x][8], "Parsed")) {
        target_phase = ModuleAnalysis::Phase::Parsed;
      } else if (!strcasecmp(&argv[x][8], "Annotated")) {
        target_phase = ModuleAnalysis::Phase::Annotated;
      } else if (!strcasecmp(&argv[x][8], "Analyzed")) {
        target_phase = ModuleAnalysis::Phase::Analyzed;
      } else if (!strcasecmp(&argv[x][8], "Imported")) {
        target_phase = ModuleAnalysis::Phase::Imported;
      } else {
        throw invalid_argument("unknown phase");
      }

    } else if (!strncmp(argv[x], "-X", 2)) {
      vector<string> debug_flags = split(&argv[x][2], ',');
      for (const auto& flag : debug_flags) {
        global.debug_flags |= debug_flag_for_name(flag.c_str());
      }

    } else {
      module_names.emplace_back(argv[x]);
    }
  }

  for (const string& module_name : module_names) {
    global.get_module_at_phase(module_name, target_phase);
  }

  return 0;
}
