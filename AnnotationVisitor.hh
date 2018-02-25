#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AST/Environment.hh"
#include "AST/SourceFile.hh"
#include "AST/PythonASTNodes.hh"
#include "AST/PythonASTVisitor.hh"
#include "Analysis.hh"



class AnnotationVisitor : public RecursiveASTVisitor {
public:
  AnnotationVisitor(GlobalAnalysis* global, ModuleAnalysis* module);
  ~AnnotationVisitor() = default;

  using RecursiveASTVisitor::visit;

  virtual void visit(ImportStatement* a);
  virtual void visit(GlobalStatement* a);
  virtual void visit(AttributeLValueReference* a);
  virtual void visit(ExceptStatement* a);
  virtual void visit(FunctionDefinition* a);
  virtual void visit(LambdaDefinition* a);
  virtual void visit(ClassDefinition* a);

  virtual void visit(UnaryOperation* a);
  virtual void visit(YieldStatement* a);
  virtual void visit(FunctionCall* a);

  virtual void visit(ModuleStatement* a);

private:
  static std::atomic<int64_t> next_function_id;

  GlobalAnalysis* global;
  ModuleAnalysis* module;

  // temporary state
  int64_t in_function_id;
  int64_t in_class_id;

  FunctionContext* current_function();
  ClassContext* current_class();

  void record_write(const std::string& name, size_t file_offset);
};
