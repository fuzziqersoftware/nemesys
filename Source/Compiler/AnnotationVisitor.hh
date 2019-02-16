#pragma once

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Environment/Value.hh"
#include "../AST/SourceFile.hh"
#include "../AST/PythonASTNodes.hh"
#include "../AST/PythonASTVisitor.hh"
#include "Contexts.hh"



class AnnotationVisitor : public RecursiveASTVisitor {
public:
  AnnotationVisitor(GlobalContext* global, ModuleContext* module);
  ~AnnotationVisitor() = default;

  using RecursiveASTVisitor::visit;

  virtual void visit(ImportStatement* a);
  virtual void visit(GlobalStatement* a);
  virtual void visit(VariableLookup* a);
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
  GlobalContext* global;
  ModuleContext* module;

  // temporary state
  int64_t in_function_id;
  int64_t in_class_id;
  bool in_class_init;
  const VariableLookup* last_variable_lookup_node;

  FunctionContext* current_function();
  ClassContext* current_class();

  void record_write(const std::string& name, size_t file_offset);
  void record_class_attribute_write(const std::string& name, size_t file_offset);
};
