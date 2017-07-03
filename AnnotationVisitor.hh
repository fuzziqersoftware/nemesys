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

  FunctionContext* current_function();

  void record_write(const std::string& name, size_t file_offset);
};
