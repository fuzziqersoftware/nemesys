#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "SourceFile.hh"
#include "PythonASTNodes.hh"
#include "Environment.hh"



class compile_error : public std::runtime_error {
public:
  compile_error(const std::string& what, ssize_t where = -1);
  virtual ~compile_error() = default;

  ssize_t where;
};



struct ClassAnalysis {
  std::string name;
  std::map<std::string, ValueType> attr_to_type;
  std::map<std::string, std::string> method_to_name;
};



class ModuleAnalysis;

struct FunctionContext {
  ModuleAnalysis* module; // Annotated; NULL for built-in functions
  int64_t id; // Annotated
  bool is_class; // Annotated

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

  std::unordered_set<std::string> globals; // Annotated
  std::map<std::string, Variable> locals; // keys Annotated, values Analyzed
  std::unordered_set<std::string> deleted_variables; // Analyzed

  std::unordered_set<Variable> return_types; // Analyzed

  // the rest of this isn't valid until Imported or later

  struct Fragment {
    Variable return_type; // Indeterminate if this fragment doesn't return
    const void* compiled;

    Fragment(Variable return_type, const void* compiled);
  };

  std::unordered_map<std::string, int64_t> arg_signature_to_fragment_id;

  std::unordered_map<int64_t, Fragment> fragments; // Compiled

  FunctionContext(ModuleAnalysis* module, int64_t id);

  // constructor for built-in functions
  FunctionContext(ModuleAnalysis* module, int64_t id, const char* name,
      const char* var_signature, Variable return_type, const void* compiled);
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
  std::shared_ptr<SourceFile> source;

  // the following are valid in the Parsed phase and later:
  std::shared_ptr<ModuleStatement> ast;

  // the following are valid in the Annotated phase and later:
  // TODO: de-derpify this by merging these two maps into one
  std::unordered_map<std::string, bool> globals_mutable;
  std::map<std::string, Variable> globals; // values invalid until Analyzed
  int64_t global_base_offset;

  int64_t num_splits; // split count for root scope

  ModuleAnalysis(const std::string& name, const std::string& source_filename);
  ~ModuleAnalysis() = default;
};



class GlobalAnalysis {
public:
  std::unordered_map<std::string, std::shared_ptr<ModuleAnalysis>> modules;
  std::vector<std::string> import_paths;

  int64_t global_space_used;

  bool debug_find_file;
  bool debug_source;
  bool debug_lexer;
  bool debug_parser;
  bool debug_annotation;
  bool debug_analysis;
  bool debug_import;

  GlobalAnalysis();
  ~GlobalAnalysis() = default;

  void print_compile_error(FILE* stream, std::shared_ptr<ModuleAnalysis> module,
      const compile_error& e);

  void advance_module_phase(std::shared_ptr<ModuleAnalysis> module,
      ModuleAnalysis::Phase phase);
  std::shared_ptr<ModuleAnalysis> get_module_at_phase(
      const std::string& module_name, ModuleAnalysis::Phase phase);
  std::string find_source_file(const std::string& module_name);

  FunctionContext* context_for_function(int64_t function_id,
      ModuleAnalysis* module_for_create = NULL);

private:
  std::unordered_set<std::shared_ptr<ModuleAnalysis>> in_progress;

  std::unordered_map<int64_t, FunctionContext> function_id_to_context;
};
