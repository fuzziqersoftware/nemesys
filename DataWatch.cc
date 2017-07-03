#include <stdint.h>
#include <stdio.h>

#include <phosg/Strings.hh>

using namespace std;


int main(int argc, char** argv) {
  char* line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  string data;
  while ((linelen = getline(&line, &linecap, stdin)) > 0) {
    data = parse_data_string(line);
    print_data(stdout, data.data(), data.size());
  }
  return 0;
}
