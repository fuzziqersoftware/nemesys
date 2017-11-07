#include "Analysis.hh"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "Debug.hh"
#include "Parser/PythonLexer.hh"
#include "Parser/PythonParser.hh"
#include "Parser/PythonASTNodes.hh"
#include "Parser/PythonASTVisitor.hh"
#include "AnnotationVisitor.hh"
#include "AnalysisVisitor.hh"
#include "CompilationVisitor.hh"
#include "Environment.hh"
#include "BuiltinFunctions.hh"
#include "Types/Dictionary.hh"
#include "Types/List.hh"

using namespace std;



compile_error::compile_error(const string& what, ssize_t where) :
    runtime_error(what), where(where) { }



BuiltinFragmentDefinition::BuiltinFragmentDefinition(
    const vector<Variable>& arg_types, Variable return_type,
    const void* compiled) : arg_types(arg_types), return_type(return_type),
    compiled(compiled) { }

BuiltinFunctionDefinition::BuiltinFunctionDefinition(const char* name,
    const std::vector<Variable>& arg_types, Variable return_type,
    const void* compiled, bool pass_exception_block, bool register_globally) :
    name(name), fragments({{arg_types, return_type, compiled}}),
    pass_exception_block(pass_exception_block),
    register_globally(register_globally) { }

BuiltinFunctionDefinition::BuiltinFunctionDefinition(const char* name,
    const std::vector<BuiltinFragmentDefinition>& fragments,
    bool pass_exception_block, bool register_globally) : name(name),
    fragments(fragments), pass_exception_block(pass_exception_block),
    register_globally(register_globally) { }

BuiltinClassDefinition::BuiltinClassDefinition(const char* name,
    const std::map<std::string, Variable>& attributes,
    const std::vector<BuiltinFunctionDefinition>& methods,
    const void* destructor, bool register_globally) : name(name),
    attributes(attributes), methods(methods), destructor(destructor),
    register_globally(register_globally) { }



ClassContext::ClassContext(ModuleAnalysis* module, int64_t id) : module(module),
    id(id), destructor(NULL), ast_root(NULL) { }

void ClassContext::populate_dynamic_attributes() {
  for (const auto& it : this->attributes) {
    if ((it.second.type != ValueType::Function) &&
        (it.second.type != ValueType::Class)) {
      if (debug_flags & DebugFlag::ShowAnalyzeDebug) {
        string type_str = it.second.str();
        fprintf(stderr, "[finalize_class] %s<%" PRId64 ">.%s = %s (dynamic)\n",
            this->name.c_str(), this->id, it.first.c_str(), type_str.c_str());
      }
      this->dynamic_attribute_indexes.emplace(
          it.first, this->dynamic_attribute_indexes.size());
    } else if (debug_flags & DebugFlag::ShowAnalyzeDebug) {
      string type_str = it.second.str();
      fprintf(stderr, "[finalize_class] %s<%" PRId64 ">.%s = %s (static)\n",
          this->name.c_str(), this->id, it.first.c_str(), type_str.c_str());
    }
  }
}

int64_t ClassContext::attribute_count() const {
  return this->dynamic_attribute_indexes.size();
}

int64_t ClassContext::instance_size() const {
  return sizeof(int64_t) * this->attribute_count() + sizeof(InstanceObject);
}

int64_t ClassContext::offset_for_attribute(const char* attribute) const {
  return this->offset_for_attribute(this->dynamic_attribute_indexes.at(attribute));
}

int64_t ClassContext::offset_for_attribute(size_t index) const {
  // attributes are stored at [instance + 8 * which + attribute_start_offset]
  return (sizeof(int64_t) * index) + sizeof(InstanceObject);
}

void ClassContext::set_attribute(void* instance, const char* attribute, int64_t value) const {
  uint8_t* p = reinterpret_cast<uint8_t*>(instance);
  *reinterpret_cast<int64_t*>(p + this->offset_for_attribute(attribute)) = value;
}



FunctionContext::Fragment::Fragment(Variable return_type, const void* compiled)
    : return_type(return_type), compiled(compiled) { }

FunctionContext::Fragment::Fragment(Variable return_type, const void* compiled,
    multimap<size_t, string>&& compiled_labels) :
    return_type(return_type), compiled(compiled),
    compiled_labels(move(compiled_labels)) { }

FunctionContext::FunctionContext(ModuleAnalysis* module, int64_t id) :
    module(module), id(id), class_id(0), ast_root(NULL), num_splits(0),
    pass_exception_block(false) { }

FunctionContext::FunctionContext(ModuleAnalysis* module, int64_t id,
    const char* name, const vector<BuiltinFragmentDefinition>& fragments,
    bool pass_exception_block) : module(module), id(id), class_id(0),
    name(name), ast_root(NULL), num_splits(0),
    pass_exception_block(pass_exception_block) {

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

    // built-in functions are allowed to have Indeterminate argument types; this
    // means they accept any type (have to be careful with this, of course)
    string signature = type_signature_for_variables(fragment_def.arg_types, true);
    int64_t fragment_id = this->arg_signature_to_fragment_id.size() + 1;
    this->arg_signature_to_fragment_id.emplace(signature, fragment_id);
    this->fragments.emplace(piecewise_construct, forward_as_tuple(fragment_id),
        forward_as_tuple(fragment_def.return_type, fragment_def.compiled));
  }
}

bool FunctionContext::is_class_init() const {
  return this->id == this->class_id;
}



// these module attributes are statically populated even for dynamic modules.
// this should match the attributes that are created automatically in the
// ModuleAnalysis constructor
static const unordered_set<string> static_initialize_module_attributes({
  "__name__", "__file__"});

ModuleAnalysis::ModuleAnalysis(const string& name, const string& filename,
    bool is_code) : phase(Phase::Initial), name(name),
    source(new SourceFile(filename, is_code)), global_base_offset(-1),
    num_splits(0), compiled(NULL), compiled_size(0) {
  // TODO: using unescape_unicode is a stupid hack, but these strings can't
  // contain backslashes anyway (right? ...right?)
  this->globals.emplace(piecewise_construct, forward_as_tuple("__name__"),
      forward_as_tuple(ValueType::Unicode, unescape_unicode(name)));
  if (is_code) {
    this->globals.emplace(piecewise_construct, forward_as_tuple("__file__"),
        forward_as_tuple(ValueType::Unicode, L"__main__"));
  } else {
    this->globals.emplace(piecewise_construct, forward_as_tuple("__file__"),
        forward_as_tuple(ValueType::Unicode, unescape_unicode(filename)));
  }
}

ModuleAnalysis::ModuleAnalysis(const string& name,
    const map<string, Variable>& globals) : phase(Phase::Initial),
    name(name), source(NULL), ast_root(NULL), globals(globals),
    global_base_offset(-1), num_splits(0), compiled(NULL) { }

int64_t ModuleAnalysis::create_builtin_function(BuiltinFunctionDefinition& def) {
  int64_t function_id = ::create_builtin_function(def);
  this->globals.emplace(def.name, Variable(ValueType::Function, function_id));
  return function_id;
}

int64_t ModuleAnalysis::create_builtin_class(BuiltinClassDefinition& def) {
  int64_t class_id = ::create_builtin_class(def);
  this->globals.emplace(def.name, Variable(ValueType::Class, class_id));
  return class_id;
}



GlobalAnalysis::GlobalAnalysis(const vector<string>& import_paths) :
    import_paths(import_paths), global_space(NULL), global_space_used(0) { }

GlobalAnalysis::~GlobalAnalysis() {
  if (this->global_space) {
    free(this->global_space);
  }
  for (const auto& it : this->bytes_constants) {
    if (debug_flags & DebugFlag::ShowRefcountChanges) {
      fprintf(stderr, "[refcount:constants] deleting Bytes constant %s\n",
          it.second->data);
    }
    delete_reference(it.second);
  }
  for (const auto& it : this->unicode_constants) {
    if (debug_flags & DebugFlag::ShowRefcountChanges) {
      fprintf(stderr, "[refcount:constants] deleting Unicode constant %ls\n",
          it.second->data);
    }
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
        if (module->source.get()) {
          shared_ptr<PythonLexer> lexer(new PythonLexer(module->source));
          if (debug_flags & DebugFlag::ShowLexDebug) {
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
          if (debug_flags & DebugFlag::ShowParseDebug) {
            fprintf(stderr, "[%s] ======== module parsed\n", module->name.c_str());
            module->ast_root->print(stderr);
            fputc('\n', stderr);
          }
        } else if (debug_flags & (DebugFlag::ShowLexDebug | DebugFlag::ShowParseDebug)) {
          fprintf(stderr, "[%s] ======== no lexing/parsing for built-in module\n", module->name.c_str());
        }

        module->phase = ModuleAnalysis::Phase::Parsed;
        break;
      }

      case ModuleAnalysis::Phase::Parsed: {
        if (module->ast_root.get()) {
          AnnotationVisitor v(this, module.get());
          try {
            module->ast_root->accept(&v);
          } catch (const compile_error& e) {
            this->print_compile_error(stderr, module.get(), e);
            throw;
          }
        }

        // reserve space for this module's globals
        module->global_base_offset = this->reserve_global_space(
            sizeof(int64_t) * module->globals.size());

        if (debug_flags & DebugFlag::ShowAnnotateDebug) {
          fprintf(stderr, "[%s] ======== module annotated\n", module->name.c_str());
          if (module->ast_root.get()) {
            module->ast_root->print(stderr);

            fprintf(stderr, "# split count: %" PRIu64 "\n", module->num_splits);
          }

          for (const auto& it : module->globals) {
            fprintf(stderr, "# global: %s\n", it.first.c_str());
          }
          fprintf(stderr, "# global space is now %p (%" PRId64 " bytes)\n",
              this->global_space, this->global_space_used);
          fputc('\n', stderr);
        }
        module->phase = ModuleAnalysis::Phase::Annotated;
        break;
      }

      case ModuleAnalysis::Phase::Annotated: {
        if (module->ast_root.get()) {
          AnalysisVisitor v(this, module.get());
          try {
            module->ast_root->accept(&v);
          } catch (const compile_error& e) {
            this->print_compile_error(stderr, module.get(), e);
            throw;
          }
        }

        if (debug_flags & DebugFlag::ShowAnalyzeDebug) {
          fprintf(stderr, "[%s] ======== module analyzed\n", module->name.c_str());
          if (module->ast_root.get()) {
            module->ast_root->print(stderr);
          }

          int64_t offset = module->global_base_offset;
          for (const auto& it : module->globals) {
            string value_str = it.second.str();
            fprintf(stderr, "# global at r13+%" PRIX64 ": %s = %s\n",
                offset, it.first.c_str(), value_str.c_str());
            offset += 8;
          }
          fputc('\n', stderr);
        }

        this->initialize_global_space_for_module(module);

        if (debug_flags & DebugFlag::ShowAnalyzeDebug) {
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
        if (module->ast_root.get()) {
          auto fragment = this->compile_scope(module.get());
          module->compiled = reinterpret_cast<void*(*)()>(const_cast<void*>(fragment.compiled));
          module->compiled_labels = move(fragment.compiled_labels);

          if (debug_flags & DebugFlag::ShowCompileDebug) {
            fprintf(stderr, "[%s] ======== executing root scope\n",
                module->name.c_str());
          }

          // all imports are done statically, so we can't translate this to a
          // python exception - just fail
          void* exc = module->compiled();
          if (exc) {
            int64_t class_id = *reinterpret_cast<int64_t*>(reinterpret_cast<int64_t*>(exc) + 2);
            ClassContext* cls = this->context_for_class(class_id);
            const char* class_name = cls ? cls->name.c_str() : "<missing>";
            throw compile_error(string_printf(
                "module root scope raised exception of class %" PRId64 " (%s)",
                class_id, class_name));
          }
        }

        if (debug_flags & DebugFlag::ShowCompileDebug) {
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
    FunctionContext* fn,
    const unordered_map<string, Variable>* local_overrides) {

  // if a context is given, then the module must match it
  if (fn && (fn->module != module)) {
    throw compile_error("module context incorrect for function");
  }

  // if a context is not given, local_overrides must not be given either
  if (!fn && local_overrides) {
    throw compile_error("local overrides cannot be given for module scope");
  }

  // create the compilation visitor
  string scope_name;
  multimap<size_t, string> compiled_labels;
  unordered_set<size_t> patch_offsets;
  unique_ptr<CompilationVisitor> v;
  if (fn) {
    auto* cls = this->context_for_class(fn->class_id);
    if (cls) {
      scope_name = string_printf("%s.%s.%s+%" PRId64, module->name.c_str(),
          cls->name.c_str(), fn->name.c_str(), fn->id);
    } else {
      scope_name = string_printf("%s.%s+%" PRId64, module->name.c_str(),
          fn->name.c_str(), fn->id);
    }
    if (local_overrides) {
      scope_name += '(';
      bool is_first = true;
      for (const auto& override : *local_overrides) {
        if (!is_first) {
          scope_name += ',';
        } else {
          is_first = false;
        }
        scope_name += override.first;
        scope_name += '=';
        scope_name += override.second.str();
      }
      scope_name += ')';
    }
    v.reset(new CompilationVisitor(this, module, fn->id, 0, local_overrides));
  } else {
    scope_name = module->name;
    v.reset(new CompilationVisitor(this, module));
  }

  // write the local overrides, if any
  if ((debug_flags & DebugFlag::ShowCompileDebug) && local_overrides &&
      !local_overrides->empty()) {
    fprintf(stderr, "[%s] ======== compiling with local overrides\n",
        scope_name.c_str());
    for (const auto& it : *local_overrides) {
      string value_str = it.second.str();
      fprintf(stderr, "%s = %s\n", it.first.c_str(), value_str.c_str());
    }
    fputc('\n', stderr);
  }

  static unordered_set<string> scopes_in_progress;
  if (!scopes_in_progress.emplace(scope_name).second) {
    throw compile_error("recursive compilation attempt");
  }

  // compile it
  try {
    if (fn) {
      fn->ast_root->accept(v.get());
    } else {
      module->ast_root->accept(v.get());
    }

  } catch (const compile_error& e) {
    scopes_in_progress.erase(scope_name);
    if (debug_flags & DebugFlag::ShowCodeSoFar) {
      fprintf(stderr, "[%s] ======== compilation failed\ncode so far:\n",
          scope_name.c_str());
      const string& compiled = v->assembler().assemble(patch_offsets,
          &compiled_labels, true);
      string disassembly = AMD64Assembler::disassemble(compiled.data(),
          compiled.size(), 0, &compiled_labels);
      fprintf(stderr, "\n%s\n", disassembly.c_str());
    }
    this->print_compile_error(stderr, module, e);
    throw;
  }
  scopes_in_progress.erase(scope_name);

  if (debug_flags & DebugFlag::ShowCompileDebug) {
    fprintf(stderr, "[%s] ======== scope compiled\n\n",
        scope_name.c_str());
  }

  Variable return_type(ValueType::None);
  if (v->return_types().size() > 1) {
    throw compile_error("scope has multiple return types");
  }
  if (!v->return_types().empty()) {
    // there's exactly one return type
    return_type = *v->return_types().begin();
  }

  string compiled = v->assembler().assemble(patch_offsets, &compiled_labels);
  const void* executable = this->code.append(compiled, &patch_offsets);
  module->compiled_size += compiled.size();

  if (debug_flags & DebugFlag::ShowAssembly) {
    fprintf(stderr, "[%s] ======== scope assembled\n", scope_name.c_str());
    uint64_t addr = reinterpret_cast<uint64_t>(executable);
    string disassembly = AMD64Assembler::disassemble(executable,
        compiled.size(), addr, &compiled_labels);
    fprintf(stderr, "\n%s\n", disassembly.c_str());
  }

  return FunctionContext::Fragment(return_type, executable, move(compiled_labels));
}

shared_ptr<ModuleAnalysis> GlobalAnalysis::get_or_create_module(
    const string& module_name, const string& filename, bool filename_is_code) {

  // if it already exists, return it
  try {
    return this->modules.at(module_name);
  } catch (const out_of_range& e) { }

  // if it doesn't exist but is a built-in module, return that
  {
    auto module = get_builtin_module(module_name);
    if (module.get()) {
      this->modules.emplace(module_name, module);
      return module;
    }
  }

  // if code is given, create a module directly from that code
  if (filename_is_code) {
    auto module = this->modules.emplace(piecewise_construct,
        forward_as_tuple(module_name),
        forward_as_tuple(new ModuleAnalysis(module_name, filename, true))).first->second;
    if (debug_flags & DebugFlag::ShowSourceDebug) {
      fprintf(stderr, "[%s] added code from memory (%zu lines, %zu bytes)\n\n",
          module_name.c_str(), module->source->line_count(),
          module->source->file_size());
    }
    return module;
  }

  // if no filename is given, search for the correct file and load it
  string found_filename;
  if (filename.empty()) {
    found_filename = this->find_source_file(module_name);
  } else {
    found_filename = filename;
  }
  auto module = this->modules.emplace(piecewise_construct,
      forward_as_tuple(module_name),
      forward_as_tuple(new ModuleAnalysis(module_name, found_filename, false))).first->second;
  if (debug_flags & DebugFlag::ShowSourceDebug) {
    fprintf(stderr, "[%s] loaded %s (%zu lines, %zu bytes)\n\n",
        module_name.c_str(), found_filename.c_str(), module->source->line_count(),
        module->source->file_size());
  }
  return module;
}

shared_ptr<ModuleAnalysis> GlobalAnalysis::get_module_at_phase(
    const string& module_name, ModuleAnalysis::Phase phase) {
  auto module = this->get_or_create_module(module_name);
  this->advance_module_phase(module, phase);
  return module;
}

string GlobalAnalysis::find_source_file(const string& module_name) {
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

FunctionContext* GlobalAnalysis::context_for_function(int64_t function_id,
    ModuleAnalysis* module_for_create) {
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

ClassContext* GlobalAnalysis::context_for_class(int64_t class_id,
    ModuleAnalysis* module_for_create) {
  if (class_id == 0) {
    return NULL;
  }
  if (class_id < 0) {
    try {
      return &builtin_class_definitions.at(class_id);
    } catch (const out_of_range& e) {
      return NULL;
    }
  }
  if (module_for_create) {
    return &(*this->class_id_to_context.emplace(piecewise_construct,
        forward_as_tuple(class_id),
        forward_as_tuple(module_for_create, class_id)).first).second;
  } else {
    try {
      return &this->class_id_to_context.at(class_id);
    } catch (const out_of_range& e) {
      return NULL;
    }
  }
}

const BytesObject* GlobalAnalysis::get_or_create_constant(const string& s,
    bool use_shared_constants) {
  if (!use_shared_constants) {
    return bytes_new(NULL, s.data(), s.size());
  }

  BytesObject* o = NULL;
  try {
    o = this->bytes_constants.at(s);
  } catch (const out_of_range& e) {
    o = bytes_new(NULL, s.data(), s.size());
    this->bytes_constants.emplace(s, o);
  }
  return o;
}

const UnicodeObject* GlobalAnalysis::get_or_create_constant(const wstring& s,
    bool use_shared_constants) {
  if (!use_shared_constants) {
    return unicode_new(NULL, s.data(), s.size());
  }

  UnicodeObject* o = NULL;
  try {
    o = this->unicode_constants.at(s);
  } catch (const out_of_range& e) {
    o = unicode_new(NULL, s.data(), s.size());
    this->unicode_constants.emplace(s, o);
  }
  return o;
}

size_t GlobalAnalysis::reserve_global_space(size_t extra_space) {
  size_t ret = this->global_space_used;
  this->global_space_used += extra_space;
  this->global_space = reinterpret_cast<int64_t*>(realloc(this->global_space,
      this->global_space_used));
  return ret;
  // TODO: if global_space moves, we'll need to update r13 everywhere, sigh...
  // in a way-distant-future multithreaded world, this probably will mean
  // blocking all threads somehow, and updating r13 in their contexts if they're
  // running nemesys code, which is an awful hack. can we do something better?
}

void GlobalAnalysis::initialize_global_space_for_module(
    shared_ptr<ModuleAnalysis> module) {

  // clear everything first
  memset(&this->global_space[module->global_base_offset / 8], 0,
      sizeof(int64_t) * module->globals.size());

  // for built-in modules, construct everything statically
  size_t slot = module->global_base_offset / 8;
  for (const auto& it : module->globals) {
    // if the module is dynamic, only initialize a few globals (which the root
    // scope doesn't initialize)
    if (module->ast_root.get() &&
        !static_initialize_module_attributes.count(it.first)) {
      continue;
    }

    if (!it.second.value_known) {
      throw compile_error(string_printf("built-in global %s has unknown value",
          it.first.c_str()));
    }

    this->global_space[slot] = this->construct_value(it.second);
    slot++;
  }
}

int64_t GlobalAnalysis::construct_value(const Variable& value,
    bool use_shared_constants) {
  switch (value.type) {
    case ValueType::None:
      return 0;

    case ValueType::Bool:
    case ValueType::Int:
    case ValueType::Float:
      // returning int_value for Float here is not an error. this function
      // returns the raw (binary) contents of the cell that this value would
      // occupy, and int_value is unioned with float_value so it accurately
      // represents the value too
      return value.int_value;

    case ValueType::Bytes:
      return reinterpret_cast<int64_t>(this->get_or_create_constant(
          *value.bytes_value, use_shared_constants));

    case ValueType::Unicode:
      return reinterpret_cast<int64_t>(this->get_or_create_constant(
          *value.unicode_value, use_shared_constants));

    case ValueType::Function:
    case ValueType::Module:
      return 0;

    case ValueType::List: {
      ListObject* l = list_new(NULL, value.list_value->size(),
          type_has_refcount(value.extension_types[0].type));
      for (size_t x = 0; x < value.list_value->size(); x++) {
        l->items[x] = reinterpret_cast<void*>(
            this->construct_value(*(*value.list_value)[x], false));
      }
      return reinterpret_cast<int64_t>(l);
    }

    case ValueType::Dict: {
      size_t (*key_length)(const void*) = NULL;
      uint8_t (*key_at)(const void*, size_t) = NULL;
      if (value.extension_types[0].type == ValueType::Bytes) {
        key_length = reinterpret_cast<size_t (*)(const void*)>(bytes_length);
        key_at = reinterpret_cast<uint8_t (*)(const void*, size_t)>(bytes_at);
      } else if (value.extension_types[0].type == ValueType::Unicode) {
        key_length = reinterpret_cast<size_t (*)(const void*)>(unicode_length);
        key_at = reinterpret_cast<uint8_t (*)(const void*, size_t)>(unicode_at);
      }

      uint64_t flags = (type_has_refcount(value.extension_types[0].type) ? DictionaryFlag::KeysAreObjects : 0) |
          (type_has_refcount(value.extension_types[1].type) ? DictionaryFlag::ValuesAreObjects : 0);
      DictionaryObject* d = dictionary_new(NULL, key_length, key_at, flags);

      for (const auto& item : *value.dict_value) {
        dictionary_insert(d,
            reinterpret_cast<void*>(this->construct_value(item.first, false)),
            reinterpret_cast<void*>(this->construct_value(*item.second, false)));
      }
      return reinterpret_cast<int64_t>(d);
    }

    case ValueType::Tuple:
    case ValueType::Set:
    case ValueType::Class:
    default: {
      string value_str = value.str();
      throw compile_error("static construction unimplemented for " + value_str);
      // TODO: implement static constructors for collections
    }
  }
}
