#include "AnnotationVisitor.hh"

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "AST/PythonLexer.hh"
#include "AST/PythonParser.hh"
#include "AST/PythonASTNodes.hh"
#include "AST/PythonASTVisitor.hh"
#include "AST/Environment.hh"
#include "BuiltinFunctions.hh"

using namespace std;



AnnotationVisitor::AnnotationVisitor(GlobalAnalysis* global,
    ModuleAnalysis* module) : global(global), module(module),
    in_function_id(0), in_class_id(0) { }

void AnnotationVisitor::visit(ImportStatement* a) {
  // AnalysisVisitor will fill in the types for these variables. here, we just
  // need to collect their names; it's important that we don't do more work
  // here (e.g. import the values) because we can't depend on other modules
  // having been analyzed yet

  auto* fn = this->current_function();
  auto* scope = fn ? &fn->locals : &this->module->globals;

  // case 3
  if (a->import_star) {
    const string& module_name = a->modules.begin()->first;
    auto module = this->global->get_module_at_phase(module_name,
        ModuleAnalysis::Phase::Annotated);

    // copy the module's globals (names only) into the current scope
    for (const auto& it : module->globals) {
      if (!scope->emplace(piecewise_construct, forward_as_tuple(it.first),
          forward_as_tuple(ValueType::Indeterminate)).second) {
        throw compile_error("name overwritten by import", a->file_offset);
      }
    }

    return;
  }

  // case 1: import entire modules, not specific names
  if (a->names.empty()) {
    for (const auto& it : a->modules) {
      // we actually don't care if the module is even parseable or not; we
      // don't need anything from it yet other than its existence
      this->global->get_module_at_phase(it.first, ModuleAnalysis::Phase::Initial);
      if (!scope->emplace(it.second, Variable(ValueType::Module, it.first)).second) {
        throw compile_error("name overwritten by import", a->file_offset);
      }
    }

    return;
  }

  // case 2: import some names from a module
  const string& module_name = a->modules.begin()->first;
  auto module = this->global->get_module_at_phase(module_name,
      ModuleAnalysis::Phase::Annotated);
  for (const auto& it : a->names) {
    if (!module->globals.count(it.first)) {
      throw compile_error("imported name " + it.first + " not defined in source module", a->file_offset);
    }
    if (!scope->emplace(piecewise_construct, forward_as_tuple(it.second),
        forward_as_tuple(ValueType::Indeterminate)).second) {
      throw compile_error("name overwritten by import", a->file_offset);
    }
  }

  this->RecursiveASTVisitor::visit(a);
}

void AnnotationVisitor::visit(GlobalStatement* a) {
  if (this->in_function_id == 0) {
    throw compile_error("global statement outside of function", a->file_offset);
  }

  auto* fn = this->current_function();
  for (const auto& name : a->names) {
    if (fn->locals.count(name)) {
      throw compile_error(string_printf(
          "variable `%s` declared before global statement", name.c_str()),
          a->file_offset);
    }
    fn->explicit_globals.emplace(name);
  }

  this->RecursiveASTVisitor::visit(a);
}

void AnnotationVisitor::visit(AttributeLValueReference* a) {
  if (!a->base.get()) {
    this->record_write(a->name, a->file_offset);
  }
  this->RecursiveASTVisitor::visit(a);
}

void AnnotationVisitor::visit(ExceptStatement* a) {
  if (!a->name.empty()) {
    this->record_write(a->name, a->file_offset);
  }
  this->RecursiveASTVisitor::visit(a);
}

void AnnotationVisitor::visit(FunctionDefinition* a) {
  visit_list(a->decorators);

  // __init__ has the same function id as the class id - this makes it easy to
  // find the constructor function for a class
  bool is_class_init = (this->in_class_id && !this->in_function_id && (a->name == "__init__"));
  if (is_class_init) {
    a->function_id = this->in_class_id;
  } else {
    a->function_id = this->next_function_id++;
  }

  int64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->function_id;

  auto* fn = this->current_function();
  fn->class_id = this->in_class_id;
  fn->name = a->name;
  fn->ast_root = a;

  for (const auto& arg : a->args.args) {
    this->record_write(arg.name, a->file_offset);
  }
  if (!a->args.varargs_name.empty()) {
    this->record_write(a->args.varargs_name, a->file_offset);
  }
  if (!a->args.varkwargs_name.empty()) {
    this->record_write(a->args.varkwargs_name, a->file_offset);
  }

  this->visit_list(a->items);
  this->in_function_id = prev_function_id;

  this->record_write(a->name, a->file_offset);
}

void AnnotationVisitor::visit(LambdaDefinition* a) {
  a->function_id = this->next_function_id++;

  int64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->function_id;

  auto* fn = this->current_function();
  fn->class_id = 0; // even if inside a class, lambdas can't be instance methods
  fn->name = string_printf("Lambda@%s$%zu+%" PRIu64, this->module->name.c_str(),
      a->file_offset, a->function_id);
  fn->ast_root = a;

  for (const auto& arg : a->args.args) {
    this->record_write(arg.name, a->file_offset);
  }
  if (!a->args.varargs_name.empty()) {
    this->record_write(a->args.varargs_name, a->file_offset);
  }
  if (!a->args.varkwargs_name.empty()) {
    this->record_write(a->args.varkwargs_name, a->file_offset);
  }

  a->result->accept(this);

  this->in_function_id = prev_function_id;
}

void AnnotationVisitor::visit(ClassDefinition* a) {
  a->class_id = this->next_function_id++;

  // classes may not be declared within functions (for now)
  if (this->in_function_id) {
    throw compile_error("classes may not be declared within functions");
  }

  int64_t prev_class_id = this->in_class_id;
  this->in_class_id = a->class_id;

  auto* cls = this->current_class();
  cls->name = a->name;
  cls->ast_root = a;

  this->RecursiveASTVisitor::visit(a);
  this->in_class_id = prev_class_id;

  this->record_write(a->name, a->file_offset);
}

void AnnotationVisitor::visit(UnaryOperation* a) {
  this->RecursiveASTVisitor::visit(a);

  if (a->oper == UnaryOperator::Yield) {
    auto* fn = this->current_function();
    if (!fn) {
      throw compile_error("yield operator outside of function definition",
          a->file_offset);
    }

    a->split_id = ++fn->num_splits;
  }
}

void AnnotationVisitor::visit(YieldStatement* a) {
  auto* fn = this->current_function();
  if (!fn) {
    throw compile_error("yield statement outside of function definition",
        a->file_offset);
  }

  this->RecursiveASTVisitor::visit(a);

  // note that this doesn't need to be a split since it doesn't return a value
}

void AnnotationVisitor::visit(FunctionCall* a) {
  this->RecursiveASTVisitor::visit(a);

  auto* fn = this->current_function();
  if (!fn) {
    a->split_id = ++this->module->num_splits;
  } else {
    a->split_id = ++fn->num_splits;
  }
}

void AnnotationVisitor::visit(ModuleStatement* a) {
  this->RecursiveASTVisitor::visit(a);

  // TODO: add sanity checks here
}



atomic<int64_t> AnnotationVisitor::next_function_id(1);

FunctionContext* AnnotationVisitor::current_function() {
  return this->global->context_for_function(this->in_function_id, this->module);
}

ClassContext* AnnotationVisitor::current_class() {
  return this->global->context_for_class(this->in_class_id, this->module);
}

void AnnotationVisitor::record_write(const string& name, size_t file_offset) {
  if (name.empty()) {
    throw compile_error("empty name in record_write", file_offset);
  }

  // builtin names can't be written
  if (builtin_names.count(name)) {
    throw compile_error("can\'t assign to builtin name", file_offset);
  }

  // if we're in a function, we're writing a local or explicit global
  auto* fn = this->current_function();
  if (fn) {
    if (!fn->explicit_globals.count(name)) {
      fn->locals.emplace(piecewise_construct, forward_as_tuple(name),
          forward_as_tuple());
    }
    return;
  }

  // if we're in a class definition, we're writing a class attribute
  auto* cls = this->current_class();
  if (cls) {
    cls->attributes.emplace(piecewise_construct, forward_as_tuple(name),
        forward_as_tuple());
    return;
  }

  // we're writing a global
  this->module->globals.emplace(piecewise_construct, forward_as_tuple(name),
      forward_as_tuple(ValueType::Indeterminate));
}
