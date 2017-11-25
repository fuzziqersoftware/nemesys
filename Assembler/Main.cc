#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>
#include <string>

#include "AMD64Assembler.hh"

using namespace std;


int main(int argc, char** argv) {

  const char* filename = NULL;
  bool parse_data = false;
  for (int x = 1; x < argc; x++) {
    if (!strcmp(argv[x], "--parse-data")) {
      parse_data = true;
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

  // if needed, parse the data string
  if (parse_data) {
    data = parse_data_string(data);
  }

  // disassemble it to stdout
  string disassembly = AMD64Assembler::disassemble(data.data(), data.size());
  fwrite(disassembly.data(), 1, disassembly.size(), stdout);

  return 0;
}
