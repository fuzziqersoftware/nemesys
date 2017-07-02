#include "Analysis.hh"

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "PythonLexer.hh"
#include "PythonParser.hh"
#include "PythonASTNodes.hh"
#include "PythonASTVisitor.hh"
#include "AnnotationVisitor.hh"
#include "AnalysisVisitor.hh"
#include "CompilationVisitor.hh"
#include "Environment.hh"

using namespace std;



// compilation strategy (nemesys_compile_module):
// 1. load and parse source file
// 2. run AnnotationVisitor on it to get function IDs, variable names, and
//    imported module names
// 3. recursively load and parse source files and run AnnotationVisitor on them
//    for all imported modules
// 4. run AnalysisVisitor on the original source file to discover as many types
//    as possible
// 5. run CompilationVisitor to build the root scope of the module, generating
//    calls to nemesys_compile_function appropriately
// TODO: the rest

// by the time 2 is done, we should be able to resolve all function IDs. by the
// time 4 is done, we should know the types of all variables (and in some cases,
// the values as well).

// functions are compiled in blocks called "fragments". a fragment is a
// contiguous chunk of a function's execution path over which we know the types
// of all variables. this means that fragments can only end when new values are
// introduced into the function, either through yield expressions or function
// calls. (note that we need to split functions even if the return value of a
// function call or yield expression isn't used, since we may need to free it.)

// example function:
// def some_function(num):
//   print('some_function...')  # <-- split #1
//   for x in range(num):  # <-- split #2 (at range() call)
//     if 2 == x:
//       continue
//     elif x > 10:
//       break
//     else:
//       print(x)  # <-- split #3
//   y = other_function(x)  # <-- split #4 (at function call)
//   return y * 2 if y < 5 else x * 3



compile_error::compile_error(const std::string& what, ssize_t where) :
    runtime_error(what), where(where) { }



FunctionContext::Fragment::Fragment(Variable return_type, const void* compiled)
    : return_type(return_type), compiled(compiled) { }

FunctionContext::FunctionContext(ModuleAnalysis* module, int64_t id) :
    module(module), id(id), is_class(false), ast_root(NULL), num_splits(0) { }

FunctionContext::FunctionContext(ModuleAnalysis* module, int64_t id,
    const char* name, const char* var_signature, Variable return_type,
    const void* compiled) : module(module), id(id), is_class(false), name(name),
    ast_root(NULL), num_splits(0), return_types({return_type}),
    arg_signature_to_fragment_id({{var_signature, 1}}),
    fragments({{1, Fragment(return_type, compiled)}}) { }



ModuleAnalysis::ModuleAnalysis(const string& name, const string& filename) :
    phase(Phase::Initial), name(name), source(new SourceFile(filename)),
    num_splits(0) { }



GlobalAnalysis::GlobalAnalysis() : import_paths({"."}), global_space_used(0) { }

static void print_source_location(FILE* stream, shared_ptr<const SourceFile> f,
    size_t offset) {
  size_t line_num = f->line_number_of_offset(offset);
  string line = f->line(line_num);
  fprintf(stream, ">>> %s\n", line.c_str());
  fputs("--- ", stream);
  size_t space_count = offset - f->line_offset(line_num);
  for (; space_count; space_count--) {
    fputc(' ', stream);
  }
  fputs("^\n", stream);
}

void GlobalAnalysis::print_compile_error(FILE* stream,
    shared_ptr<ModuleAnalysis> module, const compile_error& e) {
  if (e.where >= 0) {
    size_t line_num = module->source->line_number_of_offset(e.where);
    fprintf(stream, "[%s] failure at line %zu (offset %zd): %s\n",
        module->name.c_str(), line_num, e.where, e.what());
    print_source_location(stream, module->source, e.where);
  } else {
    fprintf(stream, "[%s] failure at indeterminate location: %s\n",
        module->name.c_str(), e.what());
  }
}

void GlobalAnalysis::advance_module_phase(shared_ptr<ModuleAnalysis> module,
    ModuleAnalysis::Phase phase) {
  if (module->phase >= phase) {
    return;
  }

  // prevent infinite recursion: advance_module_phase cannot be called for a
  // module on which it is already executing (unless it would do nothing, above)
  if (!this->in_progress.emplace(module).second) {
    throw compile_error("cyclic import dependency on module " + module->name);
  }

  while (module->phase < phase) {
    switch (module->phase) {
      case ModuleAnalysis::Phase::Initial: {
        shared_ptr<PythonLexer> lexer(new PythonLexer(module->source));
        if (this->debug_lexer) {
          fprintf(stderr, "[%s] LEXER COMPLETED\n", module->name.c_str());
          const auto& tokens = lexer->get_tokens();
          for (size_t y = 0; y < tokens.size(); y++) {
            const auto& token = tokens[y];
            fprintf(stderr, "      n:%5lu type:%15s s:%s f:%lf i:%lld off:%lu len:%lu\n",
                y, PythonLexer::Token::name_for_token_type(token.type),
                token.string_data.c_str(), token.float_data, token.int_data,
                token.text_offset, token.text_length);
          }
          fputc('\n', stderr);
        }
        PythonParser parser(lexer);
        module->ast = parser.get_root();
        if (this->debug_parser) {
          fprintf(stderr, "[%s] PARSER COMPLETED\n", module->name.c_str());
          module->ast->print(stderr);
          fputc('\n', stderr);
        }
        module->phase = ModuleAnalysis::Phase::Parsed;
        break;
      }

      case ModuleAnalysis::Phase::Parsed: {
        AnnotationVisitor v(this, module.get());
        try {
          module->ast->accept(&v);
        } catch (const compile_error& e) {
          this->print_compile_error(stderr, module, e);
          throw;
        }
        if (this->debug_annotation) {
          fprintf(stderr, "[%s] ANNOTATION COMPLETED\n", module->name.c_str());
          module->ast->print(stderr);

          fprintf(stderr, "[%s] split count: %" PRIu64 "\n", module->name.c_str(),
              module->num_splits);

          for (const auto& it : module->globals) {
            const char* mutable_str;
            try {
              mutable_str = module->globals_mutable.at(it.first) ? "mutable" : "immutable";
            } catch (const out_of_range& e) {
              mutable_str = "MISSING";
            }
            fprintf(stderr, "[%s] global: %s (%s)\n", module->name.c_str(),
                it.first.c_str(), mutable_str);
          }
        }
        module->phase = ModuleAnalysis::Phase::Annotated;
        break;
      }

      case ModuleAnalysis::Phase::Annotated: {
        AnalysisVisitor v(this, module.get());
        try {
          module->ast->accept(&v);
        } catch (const compile_error& e) {
          this->print_compile_error(stderr, module, e);
          throw;
        }
        if (this->debug_analysis) {
          fprintf(stderr, "[%s] ANALYSIS COMPLETED\n", module->name.c_str());
          module->ast->print(stderr);

          fprintf(stderr, "[%s] global base offset: %" PRIX64 "\n",
              module->name.c_str(), module->global_base_offset);

          for (const auto& it : module->globals) {
            bool is_mutable = module->globals_mutable.at(it.first);
            string value_str = it.second.str();
            fprintf(stderr, "[%s] global: %s = %s (%s)\n", module->name.c_str(),
                it.first.c_str(), value_str.c_str(),
                is_mutable ? "mutable" : "immutable");
          }
          fputc('\n', stderr);
        }
        module->phase = ModuleAnalysis::Phase::Analyzed;
        break;
      }

      case ModuleAnalysis::Phase::Analyzed: {
        // compile the root scope
        CompilationVisitor v(this, module.get());
        try {
          module->ast->accept(&v);
        } catch (const compile_error& e) {
          this->print_compile_error(stderr, module, e);

          fprintf(stderr, "[%s] code so far:\n", module->name.c_str());
          const string& compiled = v.get_compiled_code();
          print_data(stderr, compiled.data(), compiled.size());

          throw;
        }

        if (this->debug_import) {
          fprintf(stderr, "[%s] IMPORT COMPLETED\n", module->name.c_str());

          const string& compiled = v.get_compiled_code();
          print_data(stderr, compiled.data(), compiled.size());

          // TODO: print more debug info
        }
        module->phase = ModuleAnalysis::Phase::Imported;
        break;
      }

      case ModuleAnalysis::Phase::Imported:
        break; // nothing to do
    }
  }

  this->in_progress.erase(module);
}

shared_ptr<ModuleAnalysis> GlobalAnalysis::get_module_at_phase(
    const string& module_name, ModuleAnalysis::Phase phase) {
  shared_ptr<ModuleAnalysis> module;
  try {
    module = this->modules.at(module_name);
  } catch (const out_of_range& e) {
    string filename = this->find_source_file(module_name);
    if (this->debug_find_file) {
      fprintf(stdout, "[%s] reading %s\n", module_name.c_str(), filename.c_str());
    }
    module = this->modules.emplace(piecewise_construct,
        forward_as_tuple(module_name),
        forward_as_tuple(new ModuleAnalysis(module_name, filename))).first->second;
    if (this->debug_source) {
      fprintf(stdout, "[%s] loaded source (%zu lines, %zu bytes)\n",
          module_name.c_str(), module->source->line_count(),
          module->source->file_size());
      fwrite(module->source->data().data(), module->source->file_size(), 1, stderr);
    }
  }

  this->advance_module_phase(module, phase);
  return module;
}

std::string GlobalAnalysis::find_source_file(const string& module_name) {
  // TODO: support dotted module names
  for (const string& path : this->import_paths) {
    string filename = path + "/" + module_name + ".py";
    try {
      stat(filename);
      return filename;
    } catch (const cannot_stat_file&) { }
  }

  throw compile_error("can\'t find file for module " + module_name);
}

FunctionContext* GlobalAnalysis::context_for_function(
    int64_t function_id, ModuleAnalysis* module_for_create) {
  if (function_id == 0) {
    return NULL;
  }
  if (module_for_create) {
    return &(*this->function_id_to_context.emplace(piecewise_construct,
        forward_as_tuple(function_id),
        forward_as_tuple(module_for_create, function_id)).first).second;
  } else {
    return &this->function_id_to_context.at(function_id);
  }
}
