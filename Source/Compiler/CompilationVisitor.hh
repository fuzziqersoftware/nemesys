#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../AST/SourceFile.hh"
#include "../AST/PythonASTNodes.hh"
#include "../AST/PythonASTVisitor.hh"
#include "../Environment/Value.hh"
#include "../Assembler/AMD64Assembler.hh"
#include "Contexts.hh"



class CompilationVisitor : public RecursiveASTVisitor {
public:
  class terminated_by_split : public std::runtime_error {
  public:
    terminated_by_split(int64_t callsite_token);
    virtual ~terminated_by_split() = default;

    int64_t callsite_token;
  };

  CompilationVisitor(GlobalContext* global, ModuleContext* module,
      Fragment* fragment);
  ~CompilationVisitor() = default;

  AMD64Assembler& assembler();
  const std::unordered_set<Value>& return_types() const;
  size_t get_file_offset() const;

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
  GlobalContext* global;
  ModuleContext* module;
  Fragment* fragment;

  // output values
  std::unordered_set<Value> function_return_types;

  // compilation state
  union {
    int64_t available_registers; // bit mask; check for (1 << register)
    struct {
      int32_t available_int_registers;
      int32_t available_float_registers;
    };
  };
  Register target_register;
  Register float_target_register;
  int64_t stack_bytes_used;
  std::unordered_map<std::string, int64_t> variable_to_stack_offset;
  std::unordered_map<std::string, Value> local_variable_types;

  std::string return_label;
  std::string exception_return_label;
  std::vector<std::string> break_label_stack;
  std::vector<std::string> continue_label_stack;

  struct VariableLocation {
    std::string name;
    Value type;

    ModuleContext* global_module; // can be NULL for locals/attributes
    int64_t global_index;

    MemoryReference variable_mem;
    bool variable_mem_valid;

    VariableLocation();

    std::string str() const;
  };
  Value current_type;
  bool holding_reference;

  bool evaluating_instance_pointer;
  bool in_finally_block;

  // output manager
  AMD64Assembler as;

  Register reserve_register(Register which = Register::None,
      bool float_register = false);
  void release_register(Register reg, bool float_register = false);
  void release_all_registers(bool float_registers = false);
  bool register_is_available(Register which, bool float_register = false);
  Register available_register(Register preferred = Register::None,
      bool float_register = false);
  Register available_register_except(
      const std::vector<Register>& prevented_registers,
      bool float_register = false);

  int64_t write_push_reserved_registers();
  void write_pop_reserved_registers(int64_t registers);

  bool is_always_truthy(const Value& type);
  bool is_always_falsey(const Value& type);
  void write_current_truth_value_test();

  void write_code_for_value(const Value& value);

  void assert_not_evaluating_instance_pointer();

  ssize_t write_function_call_stack_prep(size_t arg_count = 0);
  void write_function_call(const MemoryReference& function_loc,
      const std::vector<MemoryReference>& args,
      const std::vector<MemoryReference>& float_args,
      ssize_t arg_stack_bytes = -1, Register return_register = Register::None,
      bool return_float = false);
  void write_function_setup(const std::string& base_label, bool setup_special_regs);
  void write_function_cleanup(const std::string& base_label, bool setup_special_regs);

  void write_add_reference(Register addr_reg);
  void write_delete_held_reference(const MemoryReference& mem);
  void write_delete_reference(const MemoryReference& mem, ValueType type);

  void write_alloc_class_instance(int64_t class_id, bool initialize_attributes = true);

  void write_raise_exception(int64_t class_id, const wchar_t* message = NULL);
  void write_create_exception_block(
      const std::vector<std::pair<std::string, std::unordered_set<int64_t>>>& label_to_class_ids,
      const std::string& exception_return_label);

  void write_push(Register reg);
  void write_push(const MemoryReference& mem);
  void write_push(int64_t value);
  void write_pop(Register reg);
  void adjust_stack(ssize_t bytes, bool write_opcode = true);
  void adjust_stack_to(ssize_t bytes, bool write_opcode = true);

  void write_load_double(Register reg, double value);
  void write_read_variable(Register target_register,
      Register float_target_register, const VariableLocation& loc);
  void write_write_variable(Register value_register,
      Register float_value_register, const VariableLocation& loc);

  VariableLocation location_for_global(ModuleContext* module,
      const std::string& name);
  VariableLocation location_for_variable(const std::string& name);
  VariableLocation location_for_attribute(ClassContext* cls,
      const std::string& name, Register instance_reg);

  FunctionContext* current_function();
};
