#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include "source_file.hh"
#include "lex.hh"
#include "parse.hh"
#include "compile.hh"

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

  long x, y;
  for (x = 1; x < argc; x++) {
    SourceFile f(argv[x]);
    if (!f.data()) {
      printf(">>>>>>>>>> FILE: %s\n", argv[x]);
      printf(">>>>> failed to load file\n");
      continue;
    }
    printf(">>>>>>>>>> FILE: %s\n", f.filename());
    printf(">>>>> filesize: %lu\n", f.filesize());
    printf(">>>>> num_lines: %lu\n", f.num_lines());

    TokenStream tokens;
    PythonAST ast;
    tokenize_string(f.data(), &tokens);
    if (tokens.error != NoLexError) {
      printf("Lexer failed with error %s at %s:%lu (%s$%lu)\n",
          name_for_tokenization_error(tokens.error), f.filename(),
          f.line_number_of_offset(tokens.failure_offset), f.filename(),
          tokens.failure_offset);
    } else {
      printf(">>>>> lex completed; tokens:%lu\n", tokens.tokens.size());
      parse_token_stream(&tokens, &ast);
      if (ast.error != NoParseError) {
        printf("Parser failed with error %s at #%lu, which is %s:%lu (%s$%lu)\n",
            name_for_parse_error(ast.error), ast.failure_offset, f.filename(),
            f.line_number_of_offset(tokens.tokens[ast.failure_offset].text_offset),
            f.filename(), tokens.tokens[ast.failure_offset].text_offset);
        if (ast.failure_explanation.length())
          printf("  Parser gave explanation: %s\n", ast.failure_explanation.c_str());
      } else {
        printf(">>>>> parse completed\n");
      }
    }

    printf(">>>>> PARSER OUTPUT\n");
    if (ast.root)
      ast.root->print(0);

    if (ast.error != NoParseError) {
      printf(">>>>> LEXER OUTPUT NEAR PARSE ERROR (%lu)\n", ast.failure_offset);
      int current_line;
      y = ast.failure_offset - 5;
      if (y < 0)
        y = 0;
      for (current_line = 0; tokens.tokens[y].text_offset > f.line_end_offset(current_line); current_line++);
      print_line(&f, current_line);
      for (; y < tokens.tokens.size(); y++) {
        while (tokens.tokens[y].text_offset > f.line_end_offset(current_line)) {
          current_line++;
          print_line(&f, current_line);
        }
        InputToken& tok = tokens.tokens[y];
        printf("      n:%5lu type:%15s s:%s f:%lf i:%lld off:%lu len:%lu\n",
            y, name_for_token_type(tok.type), tok.string_data.c_str(),
            tok.float_data, tok.int_data, tok.text_offset, tok.text_length);
      }
    }

    if (ast.error == NoParseError && tokens.error == NoLexError) {
      printf(">>>>> VISITOR OUTPUT\n");
      compile_module(ast.root);
    }
  }

  return 0;
}
