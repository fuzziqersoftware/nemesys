#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AST/Environment.hh"
#include "AST/SourceFile.hh"
#include "AST/PythonASTNodes.hh"
#include "Assembler/CodeBuffer.hh"
#include "Types/Strings.hh"



class compile_error : public std::runtime_error {
public:
  compile_error(const std::string& what, ssize_t where = -1);
  virtual ~compile_error() = default;

  ssize_t where;
};



struct BuiltinFragmentDefinition {
  std::vector<Variable> arg_types;
  Variable return_type;
  const void* compiled;

  BuiltinFragmentDefinition(const std::vector<Variable>& arg_types,
      Variable return_type, const void* compiled);
};

struct BuiltinFunctionDefinition {
  const char* name;
  std::vector<BuiltinFragmentDefinition> fragments;
  bool pass_exception_block;
  bool register_globally;

  BuiltinFunctionDefinition(const char* name,
      const std::vector<Variable>& arg_types, Variable return_type,
      const void* compiled, bool pass_exception_block, bool register_globally);
  BuiltinFunctionDefinition(const char* name,
      const std::vector<BuiltinFragmentDefinition>& fragments,
      bool pass_exception_block, bool register_globally);
};

struct BuiltinClassDefinition {
  const char* name;
  std::map<std::string, Variable> attributes;
  std::vector<BuiltinFunctionDefinition> methods;
  const void* destructor;
  bool register_globally;

  BuiltinClassDefinition(const char* name,
      const std::map<std::string, Variable>& attributes,
      const std::vector<BuiltinFunctionDefinition>& methods,
      const void* destructor, bool register_globally);
};



class ModuleAnalysis;

struct ClassContext {
  ModuleAnalysis* module; // Annotated; NULL for built-in functions
  int64_t id; // Annotated. note that __init__ has the same ID
  const void* destructor; // Compiled; generated when class def is visited

  std::string name; // Annotated
  ASTNode* ast_root; // Annotated

  // {name: (value, index)}
  std::map<std::string, Variable> attributes; // Annotated; values Analyzed
  std::unordered_map<std::string, int64_t> dynamic_attribute_indexes; // Analyzed

  ClassContext(ModuleAnalysis* module, int64_t id);

  void populate_dynamic_attributes();

  int64_t attribute_count() const;
  int64_t instance_size() const;
  int64_t offset_for_attribute(const char* attribute) const;
  int64_t offset_for_attribute(size_t index) const;
  void set_attribute(void* instance, const char* attribute, int64_t value) const;
};

struct FunctionContext {
  ModuleAnalysis* module; // Annotated; NULL for built-in functions
  int64_t id; // Annotated
  int64_t class_id; // Annotated; 0 for non-member functions

  std::string name; // Annotated
  ASTNode* ast_root; // Annotated; NULL for built-in functions

  struct Argument {
    std::string name;
    Variable default_value;
  };
  std::vector<Argument> args; // Annotated; default values Analyzed
  std::string varargs_name; // Annotated
  std::string varkwargs_name; // Annotated
  int64_t num_splits; // Annotated
  bool pass_exception_block; // Initial (always false for Python functions)

  std::unordered_set<std::string> explicit_globals; // Annotated
  std::map<std::string, Variable> locals; // keys Annotated, values Analyzed
  std::unordered_set<std::string> deleted_variables; // Analyzed

  std::unordered_set<Variable> return_types; // Analyzed

  // the rest of this isn't valid until Imported or later

  struct Fragment {
    Variable return_type;
    const void* compiled;
    std::multimap<size_t, std::string> compiled_labels;

    Fragment(Variable return_type, const void* compiled);
    Fragment(Variable return_type, const void* compiled,
        std::multimap<size_t, std::string>&& compiled_labels);
  };

  std::unordered_map<std::string, int64_t> arg_signature_to_fragment_id;

  std::unordered_map<int64_t, Fragment> fragments; // Compiled

  // constructor for dynamic functions (defined in .py files)
  FunctionContext(ModuleAnalysis* module, int64_t id);

  // constructor for builtin functions
  FunctionContext(ModuleAnalysis* module, int64_t id, const char* name,
    const std::vector<BuiltinFragmentDefinition>& fragments,
    bool pass_exception_block);

  bool is_class_init() const;
};



class ModuleAnalysis {
public:
  enum Phase {
    Initial = 0, // nothing done yet; only source file loaded
    Parsed,      // AST exists
    Annotated,   // function/class IDs assigned and names collected
    Analyzed,    // types inferred
    Imported,    // root scope has been compiled and executed
  };

  // the following are always valid:
  Phase phase;
  std::string name;
  std::shared_ptr<SourceFile> source; // NULL for built-in modules

  // the following are valid in the Parsed phase and later:
  std::shared_ptr<ModuleStatement> ast_root; // NULL for built-in modules

  // the following are valid in the Annotated phase and later:
  // TODO: de-derpify this by merging these two maps into one
  std::unordered_set<std::string> globals_mutable;
  std::map<std::string, Variable> globals; // values invalid until Analyzed
  int64_t global_base_offset;

  int64_t num_splits; // split count for root scope

  std::multimap<size_t, std::string> compiled_labels;
  void* (*compiled)();
  int64_t compiled_size;

  // constructor for imported modules
  ModuleAnalysis(const std::string& name, const std::string& filename_or_code,
      bool is_code = false);

  // constructor for built-in modules
  ModuleAnalysis(const std::string& name,
      const std::map<std::string, Variable>& globals);

  ~ModuleAnalysis() = default;

  int64_t create_builtin_function(BuiltinFunctionDefinition& def);
  int64_t create_builtin_class(BuiltinClassDefinition& def);
};



class GlobalAnalysis {
public:
  CodeBuffer code;

  std::unordered_map<std::string, std::shared_ptr<ModuleAnalysis>> modules;
  std::vector<std::string> import_paths;

  int64_t* global_space;
  int64_t global_space_used;

  std::unordered_map<std::string, BytesObject*> bytes_constants;
  std::unordered_map<std::wstring, UnicodeObject*> unicode_constants;

  GlobalAnalysis(const std::vector<std::string>& import_paths);
  ~GlobalAnalysis();

  void print_compile_error(FILE* stream, const ModuleAnalysis* module,
      const compile_error& e);

  void advance_module_phase(std::shared_ptr<ModuleAnalysis> module,
      ModuleAnalysis::Phase phase);
  FunctionContext::Fragment compile_scope(ModuleAnalysis* module,
      FunctionContext* context = NULL,
      const std::unordered_map<std::string, Variable>* local_overrides = NULL);

  std::shared_ptr<ModuleAnalysis> get_or_create_module(
      const std::string& module_name, const std::string& filename = "",
      bool filename_is_code = false);
  std::shared_ptr<ModuleAnalysis> get_module_at_phase(
      const std::string& module_name, ModuleAnalysis::Phase phase);
  std::string find_source_file(const std::string& module_name);

  FunctionContext* context_for_function(int64_t function_id,
      ModuleAnalysis* module_for_create = NULL);
  ClassContext* context_for_class(int64_t class_id,
      ModuleAnalysis* module_for_create = NULL);

  const BytesObject* get_or_create_constant(const std::string& s,
      bool use_shared_constants = true);
  const UnicodeObject* get_or_create_constant(const std::wstring& s,
      bool use_shared_constants = true);

  int64_t construct_value(const Variable& value,
      bool use_shared_constants = true);

private:
  size_t reserve_global_space(size_t extra_space);
  void initialize_global_space_for_module(
      std::shared_ptr<ModuleAnalysis> module);

  std::unordered_set<std::shared_ptr<ModuleAnalysis>> in_progress;

  std::unordered_map<int64_t, FunctionContext> function_id_to_context;
  std::unordered_map<int64_t, ClassContext> class_id_to_context;
};
