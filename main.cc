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
#include "BuiltinFunctions.hh"
#include "Modules/__nemesys__.hh"
#include "Modules/sys.hh"

using namespace std;


shared_ptr<GlobalAnalysis> global;

int main(int argc, char* argv[]) {

  if (argc < 2) {
    printf("Usage: %s --phase=PHASE module_name [module_name ...]\n", argv[0]);
    return (-1);
  }

  global.reset(new GlobalAnalysis());

  vector<const char*> sys_argv;
  bool module_is_code = false;
  for (size_t x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "-X", 2)) {
      vector<string> debug_flags = split(&argv[x][2], ',');
      for (const auto& flag : debug_flags) {
        global->debug_flags |= debug_flag_for_name(flag.c_str());
      }

    } else if (!strncmp(argv[x], "-c", 2)) {
      if (!sys_argv.empty()) {
        throw invalid_argument("-c given after other non-nemesys flags");
      }
      sys_argv.emplace_back(&argv[x][2]);
      module_is_code = true;

    } else {
      sys_argv.emplace_back(argv[x]);
    }
  }

  sys_set_argv(sys_argv);

  create_default_builtin_names();

  global->get_or_create_module("__main__", sys_argv[0], module_is_code);
  global->get_module_at_phase("__main__", ModuleAnalysis::Phase::Imported);

  return 0;
}
