#include "Contexts.hh"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "../Debug.hh"
#include "../Environment/Value.hh"
#include "../AST/PythonLexer.hh"
#include "../AST/PythonParser.hh"
#include "../AST/PythonASTNodes.hh"
#include "../Types/Instance.hh"
#include "BuiltinFunctions.hh"

using namespace std;



compile_error::compile_error(const string& what, ssize_t where) :
    runtime_error(what), where(where) { }



BuiltinFragmentDefinition::BuiltinFragmentDefinition(
    const vector<Value>& arg_types, Value return_type,
    const void* compiled) : arg_types(arg_types), return_type(return_type),
    compiled(compiled) { }

BuiltinFunctionDefinition::BuiltinFunctionDefinition(const char* name,
    const std::vector<Value>& arg_types, Value return_type,
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
    const std::map<std::string, Value>& attributes,
    const std::vector<BuiltinFunctionDefinition>& methods,
    const void* destructor, bool register_globally) : name(name),
    attributes(attributes), methods(methods), destructor(destructor),
    register_globally(register_globally) { }



Fragment::Fragment(FunctionContext* fn, size_t index,
    const std::vector<Value>& arg_types) : function(fn), index(index),
    arg_types(arg_types) { }

Fragment::Fragment(FunctionContext* fn, size_t index,
    const std::vector<Value>& arg_types, Value return_type,
    const void* compiled) : function(fn), index(index),
    arg_types(arg_types), return_type(return_type), compiled(compiled) { }

void Fragment::resolve_call_split_labels() {
  unordered_map<string, size_t> label_to_index;
  for (size_t x = 0; x < this->call_split_labels.size(); x++) {
    const string& label = this->call_split_labels[x];

    // the label can be missing if the compiler never encountered it due to an
    // earlier split; just skip it
    if (label.empty()) {
      continue;
    }

    if (!label_to_index.emplace(label, x).second) {
      throw compile_error("duplicate split label: " + label);
    }
  }

  for (const auto& it : this->compiled_labels) {
    try {
      this->call_split_offsets[label_to_index.at(it.second)] = it.first;
    } catch (const out_of_range&) { }
  }
}



ClassContext::ClassContext(ModuleContext* module, int64_t id) : module(module),
    id(id), ast_root(NULL), destructor(NULL) { }

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



FunctionContext::FunctionContext(ModuleContext* module, int64_t id) :
    module(module), id(id), class_id(0), ast_root(NULL), num_splits(0),
    pass_exception_block(false) { }

FunctionContext::FunctionContext(ModuleContext* module, int64_t id,
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

  // finally, create the fragments
  for (const auto& fragment_def : fragments) {
    this->return_types.emplace(fragment_def.return_type);
    this->fragments.emplace_back(this, this->fragments.size(),
        fragment_def.arg_types, fragment_def.return_type,
        fragment_def.compiled);
  }
}

bool FunctionContext::is_class_init() const {
  return this->id == this->class_id;
}

bool FunctionContext::is_builtin() const {
  return !this->ast_root;
}

static int64_t match_function_call_arg_types(const vector<Value>& fn_arg_types,
    const vector<Value>& arg_types) {
  if (fn_arg_types.size() != arg_types.size()) {
    return -1;
  }

  int64_t promotion_count = 0;
  for (size_t x = 0; x < arg_types.size(); x++) {
    if (arg_types[x].type == ValueType::Indeterminate) {
      throw compile_error("call argument is Indeterminate");
    }

    if (fn_arg_types[x].type == ValueType::Indeterminate) {
      promotion_count++;
      continue; // don't check extension types

    } else if (fn_arg_types[x].type != arg_types[x].type) {
      return -1; // no match
    }

    int64_t extension_match_ret = match_function_call_arg_types(
        fn_arg_types[x].extension_types, arg_types[x].extension_types);
    if (extension_match_ret < 0) {
      return extension_match_ret;
    }
    promotion_count += extension_match_ret;
  }

  return promotion_count;
}

int64_t FunctionContext::fragment_index_for_call_args(
    const vector<Value>& arg_types) {
  // go through the existing fragments and see if there are any that can satisfy
  // this call. if there are multiple matches, choose the most specific one (
  // the one that has the fewest Indeterminate substitutions)
  // TODO: this is linear in the number of fragments. make it faster somehow
  int64_t fragment_index = -1;
  int64_t best_match_score = -1;
  for (size_t x = 0; x < this->fragments.size(); x++) {
    auto& fragment = this->fragments[x];

    int64_t score = match_function_call_arg_types(fragment.arg_types, arg_types);
    if (score < 0) {
      continue; // not a match
    }

    if ((best_match_score < 0) || (score < best_match_score)) {
      fragment_index = x;
      best_match_score = score;
    }
  }

  return fragment_index;
}



// these module attributes are statically populated even for dynamic modules.
// this should match the attributes that are created automatically in the
// ModuleContext constructor
const unordered_set<string> static_initialize_module_attributes({
  "__name__", "__file__"});

ModuleContext::ModuleContext(const string& name, const string& filename,
    bool is_code) : phase(Phase::Initial), name(name),
    source(new SourceFile(filename, is_code)), global_base_offset(-1),
    root_fragment_num_splits(0), root_fragment(NULL, -1, {}), compiled_size(0) {
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

ModuleContext::ModuleContext(const string& name,
    const map<string, Value>& globals) : phase(Phase::Initial),
    name(name), source(NULL), ast_root(NULL), globals(globals),
    global_base_offset(-1), root_fragment_num_splits(0),
    root_fragment(NULL, -1, {}), compiled_size(0) { }

int64_t ModuleContext::create_builtin_function(BuiltinFunctionDefinition& def) {
  int64_t function_id = ::create_builtin_function(def);
  this->globals.emplace(def.name, Value(ValueType::Function, function_id));
  return function_id;
}

int64_t ModuleContext::create_builtin_class(BuiltinClassDefinition& def) {
  int64_t class_id = ::create_builtin_class(def);
  this->globals.emplace(def.name, Value(ValueType::Class, class_id));
  return class_id;
}



GlobalContext::GlobalContext(const vector<string>& import_paths) :
    import_paths(import_paths), global_space(NULL), global_space_used(0),
    next_callsite_token(1) { }

GlobalContext::~GlobalContext() {
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

GlobalContext::UnresolvedFunctionCall::UnresolvedFunctionCall(
    int64_t callee_function_id, const std::vector<Value>& arg_types,
    ModuleContext* caller_module, int64_t caller_function_id,
    int64_t caller_fragment_index, int64_t caller_split_id) :
    callee_function_id(callee_function_id), arg_types(arg_types),
    caller_module(caller_module), caller_function_id(caller_function_id),
    caller_fragment_index(caller_fragment_index),
    caller_split_id(caller_split_id) { }

string GlobalContext::UnresolvedFunctionCall::str() const {
  string arg_types_str;
  for (const Value& v : this->arg_types) {
    if (!arg_types_str.empty()) {
      arg_types_str += ',';
    }
    arg_types_str += v.str();
  }
  return string_printf("UnresolvedFunctionCall(%" PRId64 ", [%s], %p(%s), %" PRId64
      ", %" PRId64 ", %" PRId64 ")", this->callee_function_id, arg_types_str.c_str(),
      this->caller_module, this->caller_module->name.c_str(), this->caller_function_id,
      this->caller_fragment_index, this->caller_split_id);
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

void GlobalContext::print_compile_error(FILE* stream,
    const ModuleContext* module, const compile_error& e) {
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

shared_ptr<ModuleContext> GlobalContext::get_or_create_module(
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
        forward_as_tuple(new ModuleContext(module_name, filename, true))).first->second;
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
      forward_as_tuple(new ModuleContext(module_name, found_filename, false))).first->second;
  if (debug_flags & DebugFlag::ShowSourceDebug) {
    fprintf(stderr, "[%s] loaded %s (%zu lines, %zu bytes)\n\n",
        module_name.c_str(), found_filename.c_str(), module->source->line_count(),
        module->source->file_size());
  }
  return module;
}

string GlobalContext::find_source_file(const string& module_name) {
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

FunctionContext* GlobalContext::context_for_function(int64_t function_id,
    ModuleContext* module_for_create) {
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

ClassContext* GlobalContext::context_for_class(int64_t class_id,
    ModuleContext* module_for_create) {
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

const BytesObject* GlobalContext::get_or_create_constant(const string& s,
    bool use_shared_constants) {
  if (!use_shared_constants) {
    return bytes_new(s.data(), s.size());
  }

  BytesObject* o = NULL;
  try {
    o = this->bytes_constants.at(s);
  } catch (const out_of_range& e) {
    o = bytes_new(s.data(), s.size());
    this->bytes_constants.emplace(s, o);
  }
  return o;
}

const UnicodeObject* GlobalContext::get_or_create_constant(const wstring& s,
    bool use_shared_constants) {
  if (!use_shared_constants) {
    return unicode_new(s.data(), s.size());
  }

  UnicodeObject* o = NULL;
  try {
    o = this->unicode_constants.at(s);
  } catch (const out_of_range& e) {
    o = unicode_new(s.data(), s.size());
    this->unicode_constants.emplace(s, o);
  }
  return o;
}

size_t GlobalContext::reserve_global_space(size_t extra_space) {
  size_t ret = this->global_space_used;
  this->global_space_used += extra_space;
  this->global_space = reinterpret_cast<int64_t*>(realloc(this->global_space,
      this->global_space_used));

  // clear the new space
  for (size_t x = ret / sizeof(int64_t); x < this->global_space_used / sizeof(int64_t); x++) {
    this->global_space[x] = 0;
  }

  // TODO: if global_space moves, we'll need to update r13 everywhere, sigh...
  // in a way-distant-future multithreaded world, this probably will mean
  // blocking all threads somehow, and updating r13 in their contexts if they're
  // running nemesys code, which is an awful hack. can we do something better?

  return ret;
}
