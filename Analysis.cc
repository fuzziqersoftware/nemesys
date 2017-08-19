#include "Analysis.hh"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

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
#include "BuiltinFunctions.hh"

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



DebugFlag debug_flag_for_name(const char* name) {
  if (!strcasecmp(name, "FindFile")) {
    return DebugFlag::FindFile;
  }
  if (!strcasecmp(name, "Source")) {
    return DebugFlag::Source;
  }
  if (!strcasecmp(name, "Lexing")) {
    return DebugFlag::Lexing;
  }
  if (!strcasecmp(name, "Parsing")) {
    return DebugFlag::Parsing;
  }
  if (!strcasecmp(name, "Annotation")) {
    return DebugFlag::Annotation;
  }
  if (!strcasecmp(name, "Analysis")) {
    return DebugFlag::Analysis;
  }
  if (!strcasecmp(name, "Compilation")) {
    return DebugFlag::Compilation;
  }
  if (!strcasecmp(name, "Assembly")) {
    return DebugFlag::Assembly;
  }
  if (!strcasecmp(name, "Execution")) {
    return DebugFlag::Execution;
  }
  if (!strcasecmp(name, "All")) {
    return DebugFlag::All;
  }
  return static_cast<DebugFlag>(0);
}

compile_error::compile_error(const std::string& what, ssize_t where) :
    runtime_error(what), where(where) { }



FunctionContext::Fragment::Fragment(Variable return_type, const void* compiled)
    : return_type(return_type), compiled(compiled) { }

FunctionContext::Fragment::Fragment(Variable return_type, const void* compiled,
    std::multimap<size_t, std::string>&& compiled_labels) :
    return_type(return_type), compiled(compiled),
    compiled_labels(move(compiled_labels)) { }

FunctionContext::BuiltinFunctionFragmentDefinition::BuiltinFunctionFragmentDefinition(
    const std::vector<Variable>& arg_types, Variable return_type,
    const void* compiled) : arg_types(arg_types), return_type(return_type),
    compiled(compiled) { }

FunctionContext::FunctionContext(ModuleAnalysis* module, int64_t id) :
    module(module), id(id), is_class(false), ast_root(NULL), num_splits(0) { }

FunctionContext::FunctionContext(ModuleAnalysis* module, int64_t id,
    const char* name, const vector<Variable>& arg_types, Variable return_type,
    const void* compiled) : FunctionContext(module, id, name,
      {BuiltinFunctionFragmentDefinition(arg_types, return_type, compiled)}) { }

FunctionContext::FunctionContext(ModuleAnalysis* module, int64_t id,
    const char* name,
    const std::vector<BuiltinFunctionFragmentDefinition>& fragments) :
    module(module), id(id), is_class(false), name(name), ast_root(NULL),
    num_splits(0) {

  // populate the arguments from the first fragment definition
  for (const auto& arg : fragments[0].arg_types) {
    this->args.emplace_back();
    if (arg.type == ValueType::Indeterminate) {
      throw invalid_argument("builtin functions must have known argument types");
    } else if (arg.value_known) {
      this->args.back().default_value = arg;
    }
  }

  // now merge all the fragment argument definitions together
  for (const auto& fragment_def : fragments) {
    if (fragment_def.arg_types.size() != this->args.size()) {
      throw invalid_argument("all fragments must take the same number of arguments");
    }

    for (size_t z = 0; z < fragment_def.arg_types.size(); z++) {
      const auto& fragment_arg = fragment_def.arg_types[z];
      if (fragment_arg.type == ValueType::Indeterminate) {
        throw invalid_argument("builtin functions must have known argument types");
      } else if (fragment_arg.value_known && (this->args[z].default_value != fragment_arg)) {
        throw invalid_argument("all fragments must have the same default values");
      }
    }
  }

  // finally, build the fragment map
  for (const auto& fragment_def : fragments) {
    this->return_types.emplace(fragment_def.return_type);

    string signature = type_signature_for_variables(fragment_def.arg_types);
    int64_t fragment_id = this->arg_signature_to_fragment_id.size() + 1;
    this->arg_signature_to_fragment_id.emplace(signature, fragment_id);
    this->fragments.emplace(piecewise_construct, forward_as_tuple(fragment_id),
        forward_as_tuple(fragment_def.return_type, fragment_def.compiled));
  }
}



ModuleAnalysis::ModuleAnalysis(const string& name, const string& filename,
    bool is_code) : phase(Phase::Initial), name(name),
    source(new SourceFile(filename, is_code)), num_splits(0), compiled(NULL) {
  // TODO: using unescape_unicode is a stupid hack, but these strings can't
  // contain backslashes anyway (right? ...right?)
  this->globals.emplace("__name__", unescape_unicode(name));
  this->globals_mutable.emplace("__name__", false);
  if (is_code) {
    this->globals.emplace("__file__", L"__imm__");
    this->globals_mutable.emplace("__file__", false);
  } else {
    this->globals.emplace("__file__", unescape_unicode(filename));
    this->globals_mutable.emplace("__file__", false);
  }
}



GlobalAnalysis::GlobalAnalysis() : import_paths({"."}), global_space(NULL),
    global_space_used(0) { }

GlobalAnalysis::~GlobalAnalysis() {
  if (this->global_space) {
    free(this->global_space);
  }
  for (const auto& it : this->bytes_constants) {
    delete_reference(it.second);
  }
  for (const auto& it : this->unicode_constants) {
    delete_reference(it.second);
  }
}

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
    const ModuleAnalysis* module, const compile_error& e) {
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
        if (this->debug_flags & DebugFlag::Lexing) {
          fprintf(stderr, "[%s] ======== module lexed\n", module->name.c_str());
          const auto& tokens = lexer->get_tokens();
          for (size_t y = 0; y < tokens.size(); y++) {
            const auto& token = tokens[y];
            fprintf(stderr, "      n:%5lu type:%16s s:%s f:%lf i:%lld off:%lu len:%lu\n",
                y, PythonLexer::Token::name_for_token_type(token.type),
                token.string_data.c_str(), token.float_data, token.int_data,
                token.text_offset, token.text_length);
          }
          fputc('\n', stderr);
        }
        PythonParser parser(lexer);
        module->ast_root = parser.get_root();
        if (this->debug_flags & DebugFlag::Parsing) {
          fprintf(stderr, "[%s] ======== module parsed\n", module->name.c_str());
          module->ast_root->print(stderr);
          fputc('\n', stderr);
        }
        module->phase = ModuleAnalysis::Phase::Parsed;
        break;
      }

      case ModuleAnalysis::Phase::Parsed: {
        AnnotationVisitor v(this, module.get());
        try {
          module->ast_root->accept(&v);
        } catch (const compile_error& e) {
          this->print_compile_error(stderr, module.get(), e);
          throw;
        }
        this->update_global_space();

        if (this->debug_flags & DebugFlag::Annotation) {
          fprintf(stderr, "[%s] ======== module annotated\n", module->name.c_str());
          module->ast_root->print(stderr);

          fprintf(stderr, "# split count: %" PRIu64 "\n", module->num_splits);

          for (const auto& it : module->globals) {
            const char* mutable_str;
            try {
              mutable_str = module->globals_mutable.at(it.first) ? "mutable" : "immutable";
            } catch (const out_of_range& e) {
              mutable_str = "MISSING";
            }
            fprintf(stderr, "# global: %s (%s)\n", it.first.c_str(), mutable_str);
          }
          fprintf(stderr, "# global space is now %p (%" PRId64 " bytes)\n",
              this->global_space, this->global_space_used);
          fputc('\n', stderr);
        }
        module->phase = ModuleAnalysis::Phase::Annotated;
        break;
      }

      case ModuleAnalysis::Phase::Annotated: {
        AnalysisVisitor v(this, module.get());
        try {
          module->ast_root->accept(&v);
        } catch (const compile_error& e) {
          this->print_compile_error(stderr, module.get(), e);
          throw;
        }

        if (this->debug_flags & DebugFlag::Analysis) {
          fprintf(stderr, "[%s] ======== module analyzed\n", module->name.c_str());
          module->ast_root->print(stderr);

          int64_t offset = module->global_base_offset;
          for (const auto& it : module->globals) {
            bool is_mutable = module->globals_mutable.at(it.first);
            string value_str = it.second.str();
            fprintf(stderr, "# global at r13+%" PRIX64 ": %s = %s (%s)\n",
                offset, it.first.c_str(), value_str.c_str(),
                is_mutable ? "mutable" : "immutable");
            offset += 8;
          }
          fputc('\n', stderr);
        }

        this->initialize_global_space_for_module(module);

        if (this->debug_flags & DebugFlag::Analysis) {
          fprintf(stderr, "[%s] ======== global space updated\n",
              module->name.c_str());
          print_data(stderr, this->global_space, this->global_space_used,
              reinterpret_cast<uint64_t>(this->global_space));
          fputc('\n', stderr);
        }

        module->phase = ModuleAnalysis::Phase::Analyzed;
        break;
      }

      case ModuleAnalysis::Phase::Analyzed: {
        auto fragment = this->compile_scope(module.get());
        module->compiled = reinterpret_cast<void(*)(int64_t*)>(const_cast<void*>(fragment.compiled));
        module->compiled_labels = move(fragment.compiled_labels);

        if (this->debug_flags & DebugFlag::Execution) {
          fprintf(stderr, "[%s] ======== executing root scope\n",
              module->name.c_str());
        }

        module->compiled(this->global_space);

        if (this->debug_flags & DebugFlag::Execution) {
          fprintf(stderr, "\n[%s] ======== import complete\n\n",
              module->name.c_str());
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

FunctionContext::Fragment GlobalAnalysis::compile_scope(ModuleAnalysis* module,
    FunctionContext* context,
    const unordered_map<string, Variable>* local_overrides) {

  // if a context is given, then the module must match it
  if (context && (context->module != module)) {
    throw compile_error("module context incorrect for function");
  }

  // if a context is not given, local_overrides must not be given either
  if (!context && local_overrides) {
    throw compile_error("local overrides cannot be given for module scope");
  }

  // create the compilation visitor
  string scope_name;
  multimap<size_t, string> compiled_labels;
  unique_ptr<CompilationVisitor> v;
  if (context) {
    scope_name = string_printf("%s.%s+%" PRId64, module->name.c_str(),
          context->name.c_str(), context->id);
    v.reset(new CompilationVisitor(this, module, context->id, 0, local_overrides));
  } else {
    scope_name = module->name;
    v.reset(new CompilationVisitor(this, module));
  }

  // compile it
  try {
    if (context) {
      context->ast_root->accept(v.get());
    } else {
      module->ast_root->accept(v.get());
    }

  } catch (const compile_error& e) {
    this->print_compile_error(stderr, module, e);

    fprintf(stderr, "[%s] ======== compilation failed\ncode so far:\n",
        scope_name.c_str());
    const string& compiled = v->assembler().assemble(&compiled_labels, true);
    print_data(stderr, compiled.data(), compiled.size());
    string disassembly = AMD64Assembler::disassemble(compiled.data(),
        compiled.size(), 0, &compiled_labels);
    fprintf(stderr, "\n%s\n", disassembly.c_str());

    throw;
  }

  if (this->debug_flags & DebugFlag::Compilation) {
    fprintf(stderr, "[%s] ======== scope compiled\n\n",
        scope_name.c_str());
  }

  string compiled = v->assembler().assemble(&compiled_labels);
  const void* executable = this->code.append(compiled);

  if (this->debug_flags & DebugFlag::Assembly) {
    fprintf(stderr, "[%s] ======== scope assembled\n", scope_name.c_str());
    uint64_t addr = reinterpret_cast<uint64_t>(executable);
    print_data(stderr, compiled.data(), compiled.size(), addr);
    string disassembly = AMD64Assembler::disassemble(compiled.data(),
        compiled.size(), addr, &compiled_labels);
    fprintf(stderr, "\n%s\n", disassembly.c_str());
  }

  Variable return_type(ValueType::None);
  if (v->return_types().size() > 1) {
    throw compile_error("scope has multiple return types");
  }
  if (!v->return_types().empty()) {
    // there's exactly one return type
    return_type = *v->return_types().begin();
  }

  return FunctionContext::Fragment(return_type, executable, move(compiled_labels));
}

shared_ptr<ModuleAnalysis> GlobalAnalysis::create_module(
    const string& module_name, const string* module_code) {
  shared_ptr<ModuleAnalysis> module;
  try {
    module = this->modules.at(module_name);
  } catch (const out_of_range& e) {
    if (module_code) {
      module = this->modules.emplace(piecewise_construct,
          forward_as_tuple(module_name),
          forward_as_tuple(new ModuleAnalysis(module_name, *module_code, true))).first->second;
      if (this->debug_flags & DebugFlag::Source) {
        fprintf(stdout, "[%s] added code from memory (%zu lines, %zu bytes)\n\n",
            module_name.c_str(), module->source->line_count(),
            module->source->file_size());
      }
    } else {
      string filename = this->find_source_file(module_name);
      module = this->modules.emplace(piecewise_construct,
          forward_as_tuple(module_name),
          forward_as_tuple(new ModuleAnalysis(module_name, filename, false))).first->second;
      if (this->debug_flags & DebugFlag::Source) {
        fprintf(stdout, "[%s] loaded %s (%zu lines, %zu bytes)\n\n",
            module_name.c_str(), filename.c_str(), module->source->line_count(),
            module->source->file_size());
      }
    }
  }
  return module;
}

shared_ptr<ModuleAnalysis> GlobalAnalysis::get_module_at_phase(
    const string& module_name, ModuleAnalysis::Phase phase) {
  auto module = this->create_module(module_name);
  this->advance_module_phase(module, phase);
  return module;
}

std::string GlobalAnalysis::find_source_file(const string& module_name) {
  string module_path_name = module_name;
  for (char& ch : module_path_name) {
    if (ch == '.') {
      ch = '/';
    }
  }
  for (const string& path : this->import_paths) {
    string filename = path + "/" + module_path_name + ".py";
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
  if (function_id < 0) {
    try {
      return &builtin_function_definitions.at(function_id);
    } catch (const out_of_range& e) {
      return NULL;
    }
  }
  if (module_for_create) {
    return &(*this->function_id_to_context.emplace(piecewise_construct,
        forward_as_tuple(function_id),
        forward_as_tuple(module_for_create, function_id)).first).second;
  } else {
    try {
      return &this->function_id_to_context.at(function_id);
    } catch (const out_of_range& e) {
      return NULL;
    }
  }
}

const BytesObject* GlobalAnalysis::get_or_create_constant(const string& s) {
  BytesObject* o = NULL;
  try {
    o = this->bytes_constants.at(s);
  } catch (const out_of_range& e) {
    o = bytes_new(NULL, reinterpret_cast<const uint8_t*>(s.data()), s.size());
    this->bytes_constants.emplace(s, o);
  }
  return o;
}

const UnicodeObject* GlobalAnalysis::get_or_create_constant(const wstring& s) {
  UnicodeObject* o = NULL;
  try {
    o = this->unicode_constants.at(s);
  } catch (const out_of_range& e) {
    o = unicode_new(NULL, s.data(), s.size());
    this->unicode_constants.emplace(s, o);
  }
  return o;
}

void GlobalAnalysis::update_global_space() {
  this->global_space = reinterpret_cast<int64_t*>(realloc(this->global_space,
      this->global_space_used));
  // TODO: if global_space moves, we'll need to update r13 everywhere, sigh...
  // in a way-distant-future multithreaded world, this probably will mean
  // blocking all threads somehow, and updating r13 in their contexts if they're
  // running nemesys code, which is an awful hack. can we do something better?
}

void GlobalAnalysis::initialize_global_space_for_module(
    shared_ptr<ModuleAnalysis> module) {
  size_t slot = module->global_base_offset / 8;
  for (const auto& it : module->globals) {
    // if the global is mutable or we don't know it's value, we can't populate
    // it statically
    if (module->globals_mutable.at(it.first) || !it.second.value_known) {
      this->global_space[slot] = 0;

    } else {
      switch (it.second.type) {
        case ValueType::None:
          this->global_space[slot] = 0;
          break;

        case ValueType::Bool:
        case ValueType::Int:
          this->global_space[slot] = it.second.int_value;
          break;

        case ValueType::Float:
          *reinterpret_cast<double*>(&this->global_space[slot]) =
              it.second.float_value;
          break;

        case ValueType::Bytes:
          this->global_space[slot] = reinterpret_cast<int64_t>(
              this->get_or_create_constant(*it.second.bytes_value));
          break;

        case ValueType::Unicode:
          this->global_space[slot] = reinterpret_cast<int64_t>(
              this->get_or_create_constant(*it.second.unicode_value));
          break;

        case ValueType::List:
        case ValueType::Tuple:
        case ValueType::Set:
        case ValueType::Dict:
        case ValueType::Function:
        case ValueType::Class:
        case ValueType::Module:
        default:
          // do nothing; these types will be constructed by the module root scope
          // instead
          // TODO: implement static constructors for collections
          break;
      }
    }

    slot++;
  }
}