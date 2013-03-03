#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include "source_file.hh"
#include "lex.hh"

using namespace std;

void print_line(SourceFile* f, int line_no) {
  const char* line_data = f->line(line_no);
  int line_len = 0;
  printf("%5d ", line_no);
  while (line_data[line_len] && line_data[line_len] != '\n') {
    putc(line_data[line_len], stdout);
    line_len++;
  }
  putc('\n', stdout);
}

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

    TokenizationResult result;
    tokenize_string(f.data(), &result);
    printf(">>>>> tokenized: tokens:%lu error:%d failure_pos:%d\n",
        result.tokens.size(), result.error, result.failure_offset);
    if (result.error != NoError) {
      printf("Lexer failed with error %s at %s:%d (%s$%d)\n", name_for_tokenization_error(result.error), f.filename(), f.line_number_of_offset(result.failure_offset), f.filename(), result.failure_offset);
    }

    int current_line = 0;
    print_line(&f, 0);
    for (y = 0; y < result.tokens.size(); y++) {
      while (result.tokens[y].text_offset > f.line_end_offset(current_line)) {
        current_line++;
        print_line(&f, current_line);
      }
      InputToken& tok = result.tokens[y];
      printf("      type:%15s s:%s f:%lf i:%lld off:%d len:%d\n",
          name_for_token_type(tok.type), tok.string_data.c_str(),
          tok.float_data, tok.int_data, tok.text_offset, tok.text_length);
    }
  }

  return 0;
}
