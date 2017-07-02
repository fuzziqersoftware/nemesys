#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

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
  ModuleAnalysis::Phase target_phase = ModuleAnalysis::Phase::Initial;
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
    } else if (!strcmp(argv[x], "--debug-find-file")) {
      global.debug_find_file = true;
    } else if (!strcmp(argv[x], "--debug-source")) {
      global.debug_source = true;
    } else if (!strcmp(argv[x], "--debug-lexer")) {
      global.debug_lexer = true;
    } else if (!strcmp(argv[x], "--debug-parser")) {
      global.debug_parser = true;
    } else if (!strcmp(argv[x], "--debug-annotation")) {
      global.debug_annotation = true;
    } else if (!strcmp(argv[x], "--debug-analysis")) {
      global.debug_analysis = true;
    } else if (!strcmp(argv[x], "--debug-import")) {
      global.debug_import = true;
    } else {
      module_names.emplace_back(argv[x]);
    }
  }

  for (const string& module_name : module_names) {
    global.get_module_at_phase(module_name, target_phase);
    /* TODO: fix this code and uncomment it
    try {
      global.get_module_at_phase(module_name, target_phase);

    } catch (const PythonLexer::tokenization_error& e) {
      printf("# >>>>> lex failed with error %s at %s:%zu (%s$%zu)\n",
          PythonLexer::name_for_tokenization_error(e.error),
          f.filename().c_str(), f.line_number_of_offset(e.offset),
          f.filename().c_str(), e.offset);

    } catch (const PythonParser::parse_error& e) {
      const auto& all = lexer->all();
      printf("# >>>>> parse failed with error %s (%s) at #%zu, which is %s:%lu (%s$%lu)\n",
          PythonParser::name_for_parse_error(e.error), e.explanation.c_str(),
          e.offset, f.filename().c_str(),
          f.line_number_of_offset(all[e.offset].text_offset),
          f.filename().c_str(), all[e.offset].text_offset);

      printf("# >>>>> lexer output near parse error\n");
      ssize_t current_line;
      ssize_t y = e.offset - 5;
      if (y < 0) {
        y = 0;
      }
      for (current_line = 0;
          all[y].text_offset > f.line_end_offset(current_line);
          current_line++);

      {
        string line_data = f.line(current_line);
        fprintf(stdout, "%5d %s\n", current_line, line_data.c_str());
      }
      for (; y < lexer->all().size(); y++) {
        while (all[y].text_offset > f.line_end_offset(current_line)) {
          current_line++;
          string line_data = f.line(current_line);
          fprintf(stdout, "%5d %s\n", current_line, line_data.c_str());
        }
        const auto& tok = all[y];
        printf("      n:%5lu type:%15s s:%s f:%lf i:%lld off:%lu len:%lu\n",
            y, PythonLexer::Token::name_for_token_type(tok.type),
            tok.string_data.c_str(), tok.float_data, tok.int_data,
            tok.text_offset, tok.text_length);
      }
    } */
  }

  return 0;
}
