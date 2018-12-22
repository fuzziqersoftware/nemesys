#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>
#include <set>

#include "AMD64Assembler.hh"
#include "FileAssembler.hh"

using namespace std;



void print_usage(const char* argv0) {
  fprintf(stderr, "\
Usage: %s [filename]\n\
\n\
Assembles Intel-syntax AMD64 assembly code into binary.\n\
Output is written to stdout.\n\
If filename is not given, read from stdin.\n",
      argv0);
}


int main(int argc, char** argv) {

  const char* filename = NULL;
  for (int x = 1; x < argc; x++) {
    if (!strcmp(argv[x], "--help") || !strcmp(argv[x], "-h")) {
      print_usage(argv[0]);
      return 0;

    } else {
      if (filename) {
        fprintf(stderr, "multiple filenames given\n");
        return 1;
      }
      filename = argv[x];
    }
  }

  // load the entire file
  int fd = filename ? open(filename, O_RDONLY) : 0;
  if (fd < 0) {
    fprintf(stderr, "cannot open file %s (%d)\n", filename, errno);
    return 2;
  }
  string data = read_all(fd);
  if (fd != 0) {
    close(fd);
  }

  auto af = assemble_file(data);

  // output the errors, or output the code if there were no errors
  if (!af.errors.empty()) {
    fprintf(stdout, "Errors:\n");
    for (const string& e : af.errors) {
      fprintf(stderr, "  %s\n", e.c_str());
    }

  } else {
    if (isatty(fileno(stdout))) {
      fprintf(stdout, "Assembled code:\n");
      print_data(stdout, af.code.data(), af.code.size());
      string disassembly = AMD64Assembler::disassemble(af.code, 0, &af.label_offsets);
      fprintf(stdout, "\nDisassembly:\n%s\n", disassembly.c_str());
    } else {
      fwrite(af.code.data(), 1, af.code.size(), stdout);
    }
  }

  return 0;
}
