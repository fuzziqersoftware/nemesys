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
#include "Analysis.hh"
#include "AMD64Assembler.hh"



class CompilationVisitor : public RecursiveASTVisitor {
public:
  // note: local_overrides is the argument types
  CompilationVisitor(GlobalAnalysis* global, ModuleAnalysis* module,
      int64_t target_function_id = 0, int64_t target_split_id = 0,
      const std::unordered_map<std::string, Variable>* local_overrides = NULL);
  ~CompilationVisitor() = default;

  AMD64Assembler& assembler();
  const std::unordered_set<Variable>& return_types();

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
  // debugging info
  ssize_t file_offset;

  // environment
  GlobalAnalysis* global;
  ModuleAnalysis* module;
  std::unordered_map<std::string, Variable> local_overrides;

  // targeting
  FunctionContext* target_function;
  int64_t target_split_id;
  int64_t target_fragment_id;
  std::unordered_map<std::string, int64_t> variable_to_stack_offset;

  // output values
  std::unordered_set<Variable> function_return_types;

  // compilation state
  int64_t available_registers; // bit mask; check for (1 << register)
  Register target_register;
  int64_t stack_bytes_used;

  std::string return_label;
  // TODO: break_label and continue_label stack

  struct VariableLocation {
    std::string name;
    int64_t offset;
    bool is_global;
    Variable type;
    MemoryReference mem;
  };
  VariableLocation lvalue_target;
  Variable current_type;

  // output manager
  AMD64Assembler as;

  Register reserve_register(Register which = Register::None);
  void release_register(Register reg);
  Register available_register(Register preferred = Register::None);

  int64_t write_push_reserved_registers();
  void write_pop_reserved_registers(int64_t registers);

  bool is_always_truthy(const Variable& type);
  bool is_always_falsey(const Variable& type);
  void write_truth_value_test(Register reg, const Variable& type);

  void write_code_for_value(const Variable& value);

  ssize_t write_function_call_stack_prep(size_t arg_count);
  void write_function_call(const void* function,
      const MemoryReference* function_loc,
      const std::vector<const MemoryReference>& args,
      ssize_t arg_stack_bytes = -1, Register return_register = Register::None);
  void write_function_setup(const std::string& base_label);
  void write_function_cleanup(const std::string& base_label);

  void write_add_reference(const MemoryReference& mem);
  void write_delete_reference(const MemoryReference& mem, const Variable& type);

  void write_push(Register reg);
  void write_push(const MemoryReference& mem);
  void write_push(int64_t value);
  void write_pop(Register reg);
  void adjust_stack(ssize_t bytes);

  VariableLocation location_for_variable(const std::string& name);

  FunctionContext* current_function();
};
