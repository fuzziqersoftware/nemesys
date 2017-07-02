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
  int64_t in_function_id;

  FunctionContext* current_function();

  void record_assignment(const std::string& name, const Variable& var,
      size_t file_offset);
};
