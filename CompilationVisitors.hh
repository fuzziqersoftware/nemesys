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
#include "PythonASTVisitor.hh"
#include "Environment.hh"



class compile_error : public std::runtime_error {
public:
  compile_error(const std::string& what, ssize_t where = -1);
  virtual ~compile_error() = default;

  size_t where;
};



struct FunctionAnalysis {
  struct Argument {
    // TODO: figure out what to do when default is None for otherwise
    // strictly-typed args
    // TODO: this type should be recursive somehow
    ValueType type;
    union {
      int64_t default_int;
      bool default_bool;
      const char* default_bytes; // this is in the global_data section
      const wchar_t* default_unicode; // this is in the global_data section
    };
  };

  std::string name;
  ValueType return_type;
  std::vector<Argument> argument_types;
  bool has_varargs;
  bool has_varkwargs;
};



struct ClassAnalysis {
  std::string name;
  std::map<std::string, ValueType> attr_to_type;
  std::map<std::string, std::string> method_to_name;
};



class ModuleAnalysis {
public:
  enum Phase {
    Initial = 0, // nothing done yet; only source file loaded
    Parsed,      // AST exists
    Annotated,   // function/class IDs assigned and names collected
    Analyzed,    // types inferred
  };

  // the following are always valid:
  Phase phase;
  std::string name;
  std::shared_ptr<SourceFile> source;

  // the following are valid in the Parsed phase and later:
  std::shared_ptr<ModuleStatement> ast;

  // the following are valid in the Annotated phase and later:
  std::unordered_map<std::string, bool> globals_mutable;
  std::unordered_map<std::string, Variable> globals; // values invalid until Analyzed

  struct FunctionContext {
    bool is_class; // Annotated
    std::string name; // Annotated
    std::unordered_set<std::string> globals;           // Annotated
    std::unordered_map<std::string, Variable> locals;  // keys Annotated, values Analyzed
    std::unordered_set<std::string> deleted_variables; // Analyzed

    std::unordered_set<Variable> return_types;    // Analyzed
  };

  std::unordered_map<uint64_t, FunctionContext> function_id_to_context;

  ModuleAnalysis(const std::string& name, const std::string& source_filename);
  ~ModuleAnalysis() = default;

  FunctionContext* context_for_function(uint64_t function_id);
};



class GlobalAnalysis {
public:
  std::unordered_map<std::string, std::shared_ptr<ModuleAnalysis>> modules;
  std::vector<std::string> import_paths;

  bool debug_find_file;
  bool debug_source;
  bool debug_lexer;
  bool debug_parser;
  bool debug_annotation;
  bool debug_analysis;

  GlobalAnalysis();
  ~GlobalAnalysis() = default;

  void advance_module_phase(std::shared_ptr<ModuleAnalysis> module,
      ModuleAnalysis::Phase phase);
  std::shared_ptr<ModuleAnalysis> get_module_at_phase(
      const std::string& module_name, ModuleAnalysis::Phase phase);
  std::string find_source_file(const std::string& module_name);

private:
  std::unordered_set<std::shared_ptr<ModuleAnalysis>> in_progress;
};



class AnnotationVisitor : public RecursiveASTVisitor {
  // this visitor does multiple things:
  // - it assigns function IDs for all functions and lambdas defined in the file
  // - it collects global names for the module and local names for all functions
  //   defined in the file (indexed by function ID), as well as if each global
  //   is mutable (named in a `global` statement or written in multiple places)
  // - it collects all import statements so the relevant modules can be loaded
  //   and collected
  // this visitor modifies the AST by adding annotations for function ID. it
  // does this only for FunctionDefinition and LambdaDefinition nodes; it does
  // not do this for FunctionCall nodes since they may refer to modules that are
  // not yet imported.

public:
  AnnotationVisitor(GlobalAnalysis* global, ModuleAnalysis* module);
  ~AnnotationVisitor() = default;

  using RecursiveASTVisitor::visit;

  virtual void visit(ImportStatement* a);
  virtual void visit(GlobalStatement* a);
  virtual void visit(AttributeLValueReference* a);
  virtual void visit(ExceptStatement* a);
  virtual void visit(ArgumentDefinition* a);
  virtual void visit(FunctionDefinition* a);
  virtual void visit(LambdaDefinition* a);
  virtual void visit(ClassDefinition* a);

private:
  static std::atomic<uint64_t> next_function_id;

  GlobalAnalysis* global;
  ModuleAnalysis* module;

  // temporary state
  uint64_t in_function_id;
  bool in_function_definition;

  ModuleAnalysis::FunctionContext* current_function();

  void record_write(const std::string& name, size_t file_offset);
};



class AnalysisVisitor : public RecursiveASTVisitor {
  // this visitor does multiple things:
  // - it finds the type of each variable (global and local)
  // - it finds the values of constants (global and local)

public:
  AnalysisVisitor(GlobalAnalysis* global, ModuleAnalysis* module);
  ~AnalysisVisitor() = default;

  using RecursiveASTVisitor::visit;

  // expression evaluation
  virtual void visit(UnaryOperation* a);
  virtual void visit(BinaryOperation* a);
  virtual void visit(TernaryOperation* a);
  virtual void visit(ListConstructor* a);
  virtual void visit(SetConstructor* a);
  virtual void visit(DictConstructor* a);
  virtual void visit(TupleConstructor* a);
  virtual void visit(ListComprehension* a);
  virtual void visit(SetComprehension* a);
  virtual void visit(DictComprehension* a);
  virtual void visit(LambdaDefinition* a);
  virtual void visit(FunctionCall* a);
  virtual void visit(ArrayIndex* a);
  virtual void visit(ArraySlice* a);
  virtual void visit(IntegerConstant* a);
  virtual void visit(FloatConstant* a);
  virtual void visit(BytesConstant* a);
  virtual void visit(UnicodeConstant* a);
  virtual void visit(TrueConstant* a);
  virtual void visit(FalseConstant* a);
  virtual void visit(NoneConstant* a);
  virtual void visit(VariableLookup* a);
  virtual void visit(AttributeLookup* a);

  virtual void visit(AttributeLValueReference* a);
  virtual void visit(ArrayIndexLValueReference* a);
  virtual void visit(ArraySliceLValueReference* a);
  virtual void visit(TupleLValueReference* a);

  virtual void visit(ArgumentDefinition* a);
  virtual void visit(ModuleStatement* a);
  virtual void visit(ExpressionStatement* a);
  virtual void visit(AssignmentStatement* a);
  virtual void visit(AugmentStatement* a);
  virtual void visit(DeleteStatement* a);
  virtual void visit(ImportStatement* a);
  virtual void visit(GlobalStatement* a);
  virtual void visit(ExecStatement* a);
  virtual void visit(AssertStatement* a);
  virtual void visit(BreakStatement* a);
  virtual void visit(ContinueStatement* a);
  virtual void visit(ReturnStatement* a);
  virtual void visit(RaiseStatement* a);
  virtual void visit(YieldStatement* a);
  virtual void visit(SingleIfStatement* a);
  virtual void visit(IfStatement* a);
  virtual void visit(ElseStatement* a);
  virtual void visit(ElifStatement* a);
  virtual void visit(ForStatement* a);
  virtual void visit(WhileStatement* a);
  virtual void visit(ExceptStatement* a);
  virtual void visit(FinallyStatement* a);
  virtual void visit(TryStatement* a);
  virtual void visit(WithStatement* a);
  virtual void visit(FunctionDefinition* a);
  virtual void visit(ClassDefinition* a);

private:
  GlobalAnalysis* global;
  ModuleAnalysis* module;

  // temporary state
  Variable current_value;
  uint64_t in_function_id;

  ModuleAnalysis::FunctionContext* current_function();

  void record_assignment(const std::string& name, const Variable& var,
      size_t file_offset);
};
