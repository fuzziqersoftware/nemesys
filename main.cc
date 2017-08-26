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

void print_usage(const char* argv0) {
  printf("\
Usage:\n\
  %s [options] filename\n\
  %s [options] -c\"code\"\n\
\n\
Options:\n\
  -h, -?, --help: print this text and exit.\n\
  -X<debug>: print debugging information for the given steps.\n\
      Steps are FindFile, Source, Lexing, Parsing, Annotation, Analysis,\n\
                Compilation, Assembly and Execution.\n\
      Multiple steps can be given; separate them with commas or use -X multiple\n\
      times. -Xall enables debug output for all steps.\n\
", argv0, argv0);
}

int main(int argc, char* argv[]) {

  // sanity check
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  // parse command line options
  int64_t debug_flags = 0;
  vector<const char*> sys_argv;
  bool module_is_code = false;
  size_t x;
  for (x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "-X", 2)) {
      vector<string> debug_flag_strs = split(&argv[x][2], ',');
      for (const auto& debug_flag_str : debug_flag_strs) {
        debug_flags |= debug_flag_for_name(debug_flag_str.c_str());
      }

    } else if (!strcmp(argv[x], "-h") || !strcmp(argv[x], "-?") || !strcmp(argv[x], "--help")) {
      print_usage(argv[0]);
      return 0;

    } else if (!strncmp(argv[x], "-c", 2)) {
      sys_argv.emplace_back(&argv[x][2]);
      module_is_code = true;
      break;
    } else {
      break;
    }
  }

  // anything after -c or a filename is passed as sys.argv
  for (; x < argc; x++) {
    sys_argv.emplace_back(argv[x]);
  }

  // need a script or code to run
  if (sys_argv.empty()) {
    fprintf(stderr, "nemesys does not yet implement an interactive shell\n");
    return 1;
  }

  // set up the global environment
  global.reset(new GlobalAnalysis({"."}));
  global->debug_flags = debug_flags;
  create_default_builtin_names();
  sys_set_argv(sys_argv);

  // run the specified script/code
  global->get_or_create_module("__main__", sys_argv[0], module_is_code);
  global->get_module_at_phase("__main__", ModuleAnalysis::Phase::Imported);

  return 0;
}
