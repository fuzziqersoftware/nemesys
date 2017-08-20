#include "AnnotationVisitor.hh"

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "PythonLexer.hh"
#include "PythonParser.hh"
#include "PythonASTNodes.hh"
#include "PythonASTVisitor.hh"
#include "Environment.hh"
#include "BuiltinFunctions.hh"

using namespace std;



AnnotationVisitor::AnnotationVisitor(GlobalAnalysis* global,
    ModuleAnalysis* module) : global(global), module(module),
    in_function_id(0) { }

void AnnotationVisitor::visit(ImportStatement* a) {
  // AnalysisVisitor will fill in the types for these variables. here, we just
  // need to collect their names; it's important that we don't do more work
  // here (e.g. import the values) because we can't depend on other modules
  // having been analyzed yet

  auto* context = this->current_function();
  auto* scope = context ? &context->locals : &this->module->globals;

  // case 3
  if (a->import_star) {
    const string& module_name = a->modules.begin()->first;
    auto module = this->global->get_module_at_phase(module_name,
        ModuleAnalysis::Phase::Annotated);

    // copy the module's globals (names only) into the current scope
    for (const auto& it : module->globals) {
      if (!scope->emplace(it.first, ValueType::Indeterminate).second) {
        throw compile_error("name overwritten by import", a->file_offset);
      }
      if (!context) {
        // please don't modify names that you import
        this->module->globals_mutable[it.first] = false;
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
      if (!context) {
        // please don't modify names that you import
        this->module->globals_mutable[it.second] = false;
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
    if (!scope->emplace(it.second, ValueType::Indeterminate).second) {
      throw compile_error("name overwritten by import", a->file_offset);
    }
    if (!context) {
      // please don't modify names that you import
      this->module->globals_mutable[it.second] = false;
    }
  }

  this->RecursiveASTVisitor::visit(a);
}

void AnnotationVisitor::visit(GlobalStatement* a) {
  if (this->in_function_id == 0) {
    throw compile_error("global statement outside of function", a->file_offset);
  }

  auto* context = this->current_function();
  for (const auto& name : a->names) {
    if (context->locals.count(name)) {
      throw compile_error(string_printf(
          "variable `%s` declared before global statement", name.c_str()),
          a->file_offset);
    }
    // assume mutable if referenced explicitly in a global statement
    this->module->globals_mutable[name] = true;
    context->explicit_globals.emplace(name);
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

  a->function_id = this->next_function_id++;

  int64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->function_id;

  auto* context = this->current_function();
  context->is_class = false;
  context->name = a->name;
  context->ast_root = a;

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

  auto* context = this->current_function();
  context->is_class = false;
  context->name = string_printf("Lambda@%s$%zu+%" PRIu64,
      this->module->name.c_str(), a->file_offset, a->function_id);
  context->ast_root = a;

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

  int64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->class_id;

  auto* context = this->current_function();
  context->is_class = true;
  context->name = a->name;
  context->ast_root = a;

  this->RecursiveASTVisitor::visit(a);
  this->in_function_id = prev_function_id;

  this->record_write(a->name, a->file_offset);
}

void AnnotationVisitor::visit(UnaryOperation* a) {
  this->RecursiveASTVisitor::visit(a);

  if (a->oper == UnaryOperator::Yield) {
    auto* context = this->current_function();
    if (!context) {
      throw compile_error("yield operator outside of function definition",
          a->file_offset);
    }

    a->split_id = ++context->num_splits;
  }
}

void AnnotationVisitor::visit(YieldStatement* a) {
  auto* context = this->current_function();
  if (!context) {
    throw compile_error("yield statement outside of function definition",
        a->file_offset);
  }

  this->RecursiveASTVisitor::visit(a);

  a->split_id = ++context->num_splits;
}

void AnnotationVisitor::visit(FunctionCall* a) {
  this->RecursiveASTVisitor::visit(a);

  auto* context = this->current_function();
  if (!context) {
    a->split_id = ++this->module->num_splits;
  } else {
    a->split_id = ++context->num_splits;
  }
}

void AnnotationVisitor::visit(ModuleStatement* a) {
  this->RecursiveASTVisitor::visit(a);

  // almost done; now update the global analysis with what we learned

  // reserve space for this module's globals
  if (this->module->globals_mutable.size() != this->module->globals.size()) {
    throw compile_error("global registration is incomplete", a->file_offset);
  }
}



atomic<int64_t> AnnotationVisitor::next_function_id(1);

FunctionContext* AnnotationVisitor::current_function() {
  return this->global->context_for_function(this->in_function_id, this->module);
}

void AnnotationVisitor::record_write(const string& name, size_t file_offset) {
  if (name.empty()) {
    throw compile_error("empty name in record_write", file_offset);
  }

  // builtin names can't be written
  if (builtin_names.count(name)) {
    throw compile_error("can\'t assign to builtin name", file_offset);
  }

  auto* context = this->current_function();
  if (context) {
    if (context->explicit_globals.count(name)) {
      this->module->globals_mutable[name] = true;
    } else {
      context->locals.emplace(piecewise_construct, forward_as_tuple(name),
          forward_as_tuple());
    }
  } else {
    // global write at module level. set it to mutable if this is not the
    // first write to this variable
    this->module->globals.emplace(name, ValueType::Indeterminate);
    auto emplace_ret = this->module->globals_mutable.emplace(name, false);
    if (!emplace_ret.second) {
      emplace_ret.first->second = true;
    }
  }
}
