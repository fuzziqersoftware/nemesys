#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <phosg/Strings.hh>

#include "Debug.hh"
#include "AST/SourceFile.hh"
#include "AST/PythonLexer.hh"
#include "AST/PythonParser.hh"
#include "Compiler/BuiltinFunctions.hh"
#include "Compiler/Compile.hh"
#include "Modules/__nemesys__.hh"
#include "Modules/sys.hh"

using namespace std;


shared_ptr<GlobalContext> global;

void print_usage(const char* argv0) {
  printf("\
Usage:\n\
  %s [options] filename [arguments ...]\n\
  %s [options] -c\"code\" [arguments ...]\n\
  %s [options] -m\"module.name\" [arguments ...]\n\
\n\
Options:\n\
  -?, -h, --help: print this text and exit.\n\
  -c: run the given code instead of loading from a file. All arguments passed\n\
      after this option are passed to the program in sys.argv.\n\
  -m: find the given module on the search paths and load it instead of an\n\
      explicitly-specified file. All arguments passed after this option are\n\
      passed to the program in sys.argv.\n\
  -X<debug>: enable debug flags.\n\
      Flags which print extra messages but don\'t modify behavior:\n\
        ShowSearchDebug - show actions when looking for source files\n\
        ShowSourceDebug - show actions when reading source files\n\
        ShowLexDebug - show actions when lexing source files\n\
        ShowParseDebug - show actions when parsing source files\n\
        ShowAnnotateDebug - show actions in code annotation phase\n\
        ShowAnalyzeDebug - show actions in static analysis phase\n\
        ShowCompileDebug - show actions in compilation phase\n\
        ShowAssembly - show actions and code in assembly phase\n\
        ShowRefcountChanges - show refcount change messages\n\
        Code - combination of annotation, analysis, and compilation flags\n\
        Verbose - all debug info, no behavior changes\n\
      Flags which modify behavior:\n\
        NoInlineRefcounting - disable inline refcounting\n\
        All - enable all behavior flags and debug info\n\
      -X may be used multiple times to enable multiple flags.\n\
\n\
All arguments after a filename, -c option, or -m option are not parsed;\n\
instead, they are available to the program in sys.argv.\n\
\n\
The interactive shell is not yet implemented. Either a filename or the -c\n\
option must be given.\n\
", argv0, argv0, argv0);
}

int main(int argc, char* argv[]) {

  // sanity check
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  // parse command line options
  const char* module_spec = NULL;
  vector<const char*> sys_argv;
  bool module_is_code = false;
  bool module_is_filename = true;
  vector<string> import_paths({"."});
  int x;
  for (x = 1; x < argc; x++) {
    if (!strncmp(argv[x], "-X", 2)) {
      vector<string> debug_flag_strs = split(&argv[x][2], ',');
      for (const auto& debug_flag_str : debug_flag_strs) {
        debug_flags |= debug_flag_for_name(debug_flag_str.c_str());
      }

    } else if (!strncmp(argv[x], "-A", 2)) {
      import_paths.emplace_back(&argv[x][2]);

    } else if (!strcmp(argv[x], "-h") || !strcmp(argv[x], "-?") || !strcmp(argv[x], "--help")) {
      print_usage(argv[0]);
      return 0;

    } else if (!strncmp(argv[x], "-c", 2)) {
      module_spec = &argv[x][2];
      sys_argv.emplace_back("-c");
      module_is_code = true;
      break;

    } else if (!strncmp(argv[x], "-m", 2)) {
      module_spec = &argv[x][2];
      sys_argv.emplace_back("-m"); // this gets overwritten later
      module_is_filename = false;
      break;

    } else {
      module_spec = argv[x];
      break;
    }
  }

  // anything after -c or a filename is passed as sys.argv
  for (; x < argc; x++) {
    sys_argv.emplace_back(argv[x]);
  }

  // need a script or code to run
  if (!module_spec || sys_argv.empty()) {
    fprintf(stderr, "nemesys does not yet implement an interactive shell\n");
    return 1;
  }

  // set up the global environment
  global.reset(new GlobalContext(import_paths));
  create_default_builtin_names();

  // populate the sys module appropriately
  const char* argv0_realpath = realpath(argv[0], NULL);
  if (!argv0_realpath) {
    sys_set_executable("");
  } else {
    sys_set_executable(argv0_realpath);
  }
  sys_set_argv(sys_argv);

  // find the module if necessary
  string found_filename;
  if (!module_is_filename) {
    found_filename = global->find_source_file(module_spec);
    sys_argv[0] = found_filename.c_str();
  }

  // run the specified script/code
  auto module = global->get_or_create_module("__main__", module_spec, module_is_code);
  advance_module_phase(global.get(), module.get(), ModuleContext::Phase::Imported);

  return 0;
}
