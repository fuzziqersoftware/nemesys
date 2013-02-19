#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include "source_file.hh"

using namespace std;

int main(int argc, char* argv[]) {

  if (argc < 2) {
    printf("Usage: %s file1 [file2 ...]\n", argv[0]);
    return (-1);
  }

  int x, y;
  for (x = 1; x < argc; x++) {
    SourceFile f(argv[x]);
    printf(">>>>>>>>>> FILE: %s\n", f.filename());
    printf(">>>>> filesize: %d\n", f.filesize());
    printf(">>>>> num_lines: %d\n", f.num_lines());
    for (y = 0; y < f.num_lines(); y++) {
      printf("%5d %s\n", y, f.line(y));
    }
  }

  return 0;
}
