#include "AnalysisVisitor.hh"

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



AnalysisVisitor::AnalysisVisitor(GlobalAnalysis* global, ModuleAnalysis* module)
    : global(global), module(module), in_function_id(0) { }

void AnalysisVisitor::visit(UnaryOperation* a) {
  a->expr->accept(this);
  try {
    this->current_value = execute_unary_operator(a->oper, this->current_value);
  } catch (const exception& e) {
    throw compile_error(string_printf(
        "unary operator execution failed: %s", e.what()), a->file_offset);
  }
}

void AnalysisVisitor::visit(BinaryOperation* a) {
  a->left->accept(this);
  Variable left = move(this->current_value);

  a->right->accept(this);

  try {
    this->current_value = execute_binary_operator(a->oper, left,
        this->current_value);
  } catch (const exception& e) {
    throw compile_error(string_printf(
        "binary operator execution failed: %s", e.what()), a->file_offset);
  }
}

void AnalysisVisitor::visit(TernaryOperation* a) {
  a->left->accept(this);
  Variable left = move(this->current_value);

  a->center->accept(this);
  Variable center = move(this->current_value);

  a->right->accept(this);

  try {
    this->current_value = execute_ternary_operator(a->oper, left, center,
        this->current_value);
  } catch (const exception& e) {
    throw compile_error(string_printf(
        "ternary operator execution failed: %s", e.what()), a->file_offset);
  }
}

void AnalysisVisitor::visit(ListConstructor* a) {
  Variable list(vector<shared_ptr<Variable>>(), false);
  for (auto item : a->items) {
    item->accept(this);
    list.list_value->emplace_back(new Variable(move(this->current_value)));
  }
  this->current_value = move(list);
}

void AnalysisVisitor::visit(SetConstructor* a) {
  Variable set((unordered_set<Variable>()));
  for (auto item : a->items) {
    item->accept(this);
    set.set_value->emplace(move(this->current_value));
  }
  this->current_value = move(set);
}

void AnalysisVisitor::visit(DictConstructor* a) {
  Variable dict((unordered_map<Variable, shared_ptr<Variable>>()));
  for (auto item : a->items) {
    item.first->accept(this);
    Variable key(move(this->current_value));
    item.second->accept(this);
    dict.dict_value->emplace(piecewise_construct, forward_as_tuple(move(key)),
        forward_as_tuple(new Variable(move(this->current_value))));
  }
  this->current_value = move(dict);
}

void AnalysisVisitor::visit(TupleConstructor* a) {
  Variable list(vector<shared_ptr<Variable>>(), true);
  for (auto item : a->items) {
    item->accept(this);
    list.list_value->emplace_back(new Variable(move(this->current_value)));
  }
  this->current_value = move(list);
}

void AnalysisVisitor::visit(ListComprehension* a) {
  // for now, just make these unknown-value lists
  this->current_value = Variable(ValueType::List);
}

void AnalysisVisitor::visit(SetComprehension* a) {
  // for now, just make these unknown-value sets
  this->current_value = Variable(ValueType::Set);
}

void AnalysisVisitor::visit(DictComprehension* a) {
  // for now, just make these unknown-value dicts
  this->current_value = Variable(ValueType::Dict);
}

void AnalysisVisitor::visit(LambdaDefinition* a) {
  // TODO: reduce code duplication between here and FunctionDefinition

  // assign all the arguments as Indeterminate for now; we'll come back and
  // fix them later

  int64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->function_id;
  auto* context = this->current_function();

  for (auto& arg : a->args.args) {
    // copy the argument definition into the function context
    context->args.emplace_back();
    auto& new_arg = context->args.back();
    new_arg.name = arg.name;
    if (arg.default_value.get()) {
      arg.default_value->accept(this);
      new_arg.default_value = move(this->current_value);
      if (new_arg.default_value.type == ValueType::Indeterminate) {
        throw compile_error("default value has Indeterminate type", a->file_offset);
      }
      if (!new_arg.default_value.value_known) {
        throw compile_error("can\'t resolve default value", a->file_offset);
      }
    }
  }
  context->varargs_name = a->args.varargs_name;
  context->varkwargs_name = a->args.varkwargs_name;

  a->result->accept(this);
  context->return_types.emplace(move(this->current_value));

  this->in_function_id = prev_function_id;

  this->current_value = Variable(a->function_id, false);
}

void AnalysisVisitor::visit(FunctionCall* a) {
  // the function reference had better be a function
  a->function->accept(this);
  if (this->current_value.type != ValueType::Function) {
    throw compile_error("cannot call a non-function object", a->file_offset);
  }
  Variable function = move(this->current_value);

  // we probably can't know the function's return type/value yet, but we'll try
  // to figure it out
  this->current_value = Variable(ValueType::Indeterminate);

  // if we know the function's id, annotate the AST node with it
  if (function.value_known) {
    a->callee_function_id = function.function_id;

    // if the callee is built-in (has no module) or is in a module in the
    // Analyzed phase or later, then we should know its possible return types
    // TODO: this doesn't work for functions defined in the same module; ideally
    // this would happen in a separate pass
    auto* callee_context = this->global->context_for_function(a->callee_function_id);
    if (!callee_context->module || (callee_context->module->phase >= ModuleAnalysis::Phase::Analyzed)) {
      if (callee_context->return_types.empty()) {
        this->current_value = Variable(ValueType::None);
      } else if (callee_context->return_types.size() == 1) {
        this->current_value = *callee_context->return_types.begin();
      }
    }
  }

  // if we know the return type, we can cancel this split - it won't affect the
  // local variable signature
  if (this->current_value.type != ValueType::Indeterminate) {
    a->split_id = 0;
  }

  // now visit the arg values
  for (auto& arg : a->args) {
    arg->accept(this);
  }
  for (auto& it : a->kwargs) {
    it.second->accept(this);
  }
}

void AnalysisVisitor::visit(ArrayIndex* a) {
  a->array->accept(this);
  if (this->current_value.type == ValueType::Indeterminate) {
    // don't even visit the index; we can't know anything about the result type
    return;
  }

  Variable array = move(this->current_value);

  a->index->accept(this);

  // integer indexes
  if ((array.type == ValueType::Bytes) || (array.type == ValueType::Unicode) ||
      (array.type == ValueType::List) || (array.type == ValueType::Tuple)) {

    // the index has to be a Bool or Int or Indeterminate (in the last case,
    // value_known will be false)
    if ((this->current_value.type != ValueType::Bool) &&
        (this->current_value.type != ValueType::Int) &&
        (this->current_value.type != ValueType::Indeterminate)) {
      throw compile_error("array subscript is not Bool or Int", a->file_offset);
    }

    // if we don't know the array value, we can't know the result type
    if (!array.value_known) {
      this->current_value = Variable(ValueType::Indeterminate);
      return;
    }
  }

  if (array.type == ValueType::Bytes) {
    // if the array is empty, all subscript references throw IndexError
    if (array.bytes_value->empty()) {
      // TODO: we need to throw an exception inside the program, not a compiler
      // error!
      throw compile_error("bytes is empty", a->file_offset);
    }

    // if we know the array value but not the index, we can know the result type
    // TODO: this is technically not true; may need to throw IndexError instead
    if (!this->current_value.value_known) {
      this->current_value = Variable(ValueType::Bytes);
      return;
    }

    // get the appropriate item and return it
    int64_t index = this->current_value.int_value;
    if (index < 0) {
      index += static_cast<int64_t>(array.bytes_value->size());
    }
    if ((index < 0) || (index >= static_cast<int64_t>(array.bytes_value->size()))) {
      // TODO: IndexError here too
      throw compile_error("bytes index out of range");
    }
    this->current_value = Variable(array.bytes_value->substr(index, 1));

  } else if (array.type == ValueType::Unicode) {
    // TODO: deduplicate with the above case somehow

    // if the array is empty, all subscript references throw IndexError
    if (array.unicode_value->empty()) {
      // TODO: we need to throw an exception inside the program, not a compiler
      // error!
      throw compile_error("unicode is empty", a->file_offset);
    }

    // if we know the array value but not the index, we can know the result type
    // TODO: this is technically not true; may need to throw IndexError instead
    if (!this->current_value.value_known) {
      this->current_value = Variable(ValueType::Unicode);
      return;
    }

    // get the appropriate item and return it
    int64_t index = this->current_value.int_value;
    if (index < 0) {
      index += static_cast<int64_t>(array.unicode_value->size());
    }
    if ((index < 0) || (index >= static_cast<int64_t>(array.unicode_value->size()))) {
      // TODO: IndexError here too
      throw compile_error("unicode index out of range");
    }
    this->current_value = Variable(array.unicode_value->substr(index, 1));

  } else if ((array.type == ValueType::List) || (array.type == ValueType::Tuple)) {
    // if the array is empty, all subscript references throw IndexError
    if (array.list_value->empty()) {
      // TODO: we need to throw an exception inside the program, not a compiler
      // error!
      throw compile_error("array is empty", a->file_offset);
    }

    // if we know the array value but not the index, we can know the result type
    // if all items in the array have the same type
    // TODO: this is technically not true; may need to throw IndexError instead
    if (!this->current_value.value_known) {
      // we know list_value isn't empty here
      ValueType return_type = (*array.list_value)[0]->type;
      for (const auto& item : *array.list_value) {
        if (return_type != item->type) {
          this->current_value = Variable(ValueType::Indeterminate);
          return;
        }
      }
      this->current_value = Variable(return_type);
      return;
    }

    // get the appropriate item and return it
    int64_t index = this->current_value.int_value;
    if (index < 0) {
      index += static_cast<int64_t>(array.list_value->size());
    }
    if ((index < 0) || (index >= static_cast<int64_t>(array.list_value->size()))) {
      // TODO: IndexError here too
      throw compile_error("array index out of range");
    }
    this->current_value = *(*array.list_value)[index];

  // arbitrary indexes
  } else if (array.type == ValueType::Dict) {
    // if we don't know the dict value, we can't know the result type
    if (!array.value_known) {
      this->current_value = Variable(ValueType::Indeterminate);
      return;
    }

    // if the dict is empty, all subscript references throw KeyError
    if (array.dict_value->empty()) {
      // TODO: we need to throw an exception inside the program, not a compiler
      // error!
      throw compile_error("dict is empty", a->file_offset);
    }

    // if we know the dict value but not the index, we can know the result type
    // if all values in the dict have the same type
    // TODO: this is technically not true; may need to throw KeyError instead
    if (!this->current_value.value_known) {
      auto it = array.dict_value->begin();
      ValueType return_type = it->second->type;
      for (; it != array.dict_value->end(); it++) {
        if (return_type != it->second->type) {
          this->current_value = Variable(ValueType::Indeterminate);
          return;
        }
      }
      this->current_value = Variable(return_type);
      return;
    }

    // get the appropriate item and return it
    try {
      this->current_value = *array.dict_value->at(this->current_value);
    } catch (const out_of_range& e) {
      throw compile_error("key does not exist in dict");
    }

  // other types don't support subscripts
  } else {
    string array_str = array.str();
    string index_str = this->current_value.str();
    throw compile_error(string_printf("invalid subscript reference %s[%s]",
        array_str.c_str(), index_str.c_str()), a->file_offset);
  }
}

void AnalysisVisitor::visit(ArraySlice* a) {
  // TODO
  throw compile_error("array slices are currently not supported", a->file_offset);
}

void AnalysisVisitor::visit(IntegerConstant* a) {
  this->current_value = Variable(a->value);
}

void AnalysisVisitor::visit(FloatConstant* a) {
  this->current_value = Variable(a->value);
}

void AnalysisVisitor::visit(BytesConstant* a) {
  this->current_value = Variable(a->value);
}

void AnalysisVisitor::visit(UnicodeConstant* a) {
  this->current_value = Variable(a->value);
}

void AnalysisVisitor::visit(TrueConstant* a) {
  this->current_value = Variable(true);
}

void AnalysisVisitor::visit(FalseConstant* a) {
  this->current_value = Variable(false);
}

void AnalysisVisitor::visit(NoneConstant* a) {
  this->current_value = Variable(ValueType::None);
}

void AnalysisVisitor::visit(VariableLookup* a) {
  // if the name is built-in, use that instead
  try {
    this->current_value = builtin_names.at(a->name);
    return;
  } catch (const out_of_range& e) { }

  try {
    if (this->in_function_id) {
      auto* context = this->current_function();
      if (context->globals.count(a->name)) {
        this->current_value = this->module->globals.at(a->name);
      } else {
        try {
          this->current_value = context->locals.at(a->name);
        } catch (const out_of_range& e) {
          this->current_value = this->module->globals.at(a->name);
        }
      }
    } else {
      // all lookups outside of a function are globals
      try {
        this->current_value = this->module->globals.at(a->name);
      } catch (const std::out_of_range& e) {
        throw compile_error("global " + a->name + " does not exist", a->file_offset);
      }
    }

  } catch (const out_of_range& e) {
    throw compile_error("variable " + a->name + " does not exist", a->file_offset);
  }
}

void AnalysisVisitor::visit(AttributeLookup* a) {
  a->base->accept(this);

  switch (this->current_value.type) {
    // this is technically a failure of the compiler
    case ValueType::Indeterminate:
      throw compile_error("attribute lookup on Indeterminate variable", a->file_offset);

    // these have attributes, but most programs don't use them
    case ValueType::None:
      throw compile_error("attribute lookup on None value", a->file_offset);
    case ValueType::Bool:
      throw compile_error("attribute lookup on Bool value", a->file_offset);
    case ValueType::Int:
      throw compile_error("attribute lookup on Int value", a->file_offset);
    case ValueType::Float:
      throw compile_error("attribute lookup on Float value", a->file_offset);
    case ValueType::Function:
      throw compile_error("attribute lookup on Function value", a->file_offset);

    // these have attributes and we should implement them
    // TODO: implement them
    case ValueType::Bytes:
      throw compile_error("attribute lookup on Bytes value", a->file_offset);
    case ValueType::Unicode:
      throw compile_error("attribute lookup on Unicode value", a->file_offset);
    case ValueType::List:
      throw compile_error("attribute lookup on List value", a->file_offset);
    case ValueType::Tuple:
      throw compile_error("attribute lookup on Tuple value", a->file_offset);
    case ValueType::Set:
      throw compile_error("attribute lookup on Set value", a->file_offset);
    case ValueType::Dict:
      throw compile_error("attribute lookup on Dict value", a->file_offset);
    case ValueType::Class:
      throw compile_error("attribute lookup on Class value", a->file_offset);
    case ValueType::Module:
      throw compile_error("attribute lookup on Module value", a->file_offset);
  }
}

void AnalysisVisitor::visit(TupleLValueReference* a) {
  // in this visitor, we visit the values before the unpacking tuples, so we
  // can expect this->current_value to be accurate

  if ((this->current_value.type != ValueType::List) && (this->current_value.type != ValueType::Tuple)) {
    throw compile_error("cannot unpack something that\'s not a List or Tuple", a->file_offset);
  }
  if (!this->current_value.value_known) {
    throw compile_error("cannot unpack unknown values", a->file_offset);
  }
  if (this->current_value.list_value->size() != a->items.size()) {
    throw compile_error("unpacking format length doesn\'t match List/Tuple count", a->file_offset);
  }

  Variable base_value = move(this->current_value);
  for (size_t x = 0; x < a->items.size(); x++) {
    this->current_value = move(*(*base_value.list_value)[x]);
    a->items[x]->accept(this);
  }
}

void AnalysisVisitor::visit(ArrayIndexLValueReference* a) {
  // TODO: for now ignore these
}

void AnalysisVisitor::visit(ArraySliceLValueReference* a) {
  // TODO: for now ignore these
}

void AnalysisVisitor::visit(AttributeLValueReference* a) {
  if (!a->base.get() && builtin_names.count(a->name)) {
    throw compile_error("cannot reassign built-in name " + a->name, a->file_offset);
  }
  // TODO: for now disregard attribute writes
  if (!a->base.get()) {
    this->record_assignment(a->name, this->current_value, a->file_offset);
  }
}

// statement visitation
void AnalysisVisitor::visit(ModuleStatement* a) {
  // this is the root call

  for (auto& item : a->items) {
    item->accept(this);
  }
}

void AnalysisVisitor::visit(ExpressionStatement* a) {
  // these are usually function calls or yield statements. in fact, if they
  // don't contain any function calls or yield statements, they cannot have
  // side effects, so we can disregard them entirely. for now we'll just
  // evaluate/analyze them and discard the result.
  // TODO: implement this optimization in the future
  a->expr->accept(this);
}

void AnalysisVisitor::visit(AssignmentStatement* a) {
  // evaluate expr
  a->value->accept(this);

  // assign to value (the LValueReference visitors will do this)
  a->target->accept(this);
}

void AnalysisVisitor::visit(AugmentStatement* a) {
  // TODO
  throw compile_error("AugmentStatement not yet implemented", a->file_offset);
}

void AnalysisVisitor::visit(DeleteStatement* a) {
  auto* context = this->current_function();
  if (context) {
    // TODO
    throw compile_error("DeleteStatement not yet implemented", a->file_offset);
  } else {
    // TODO: do we need to support this? seems unlikely
    throw compile_error("DeleteStatement only supported in functions", a->file_offset);
  }
}

void AnalysisVisitor::visit(ImportStatement* a) {
  // this is similar to AnnotationVisitor, except we copy values too, and we
  // expect all the names to already exist in the target scope

  auto* context = this->current_function();
  auto* scope = context ? &context->locals : &this->module->globals;

  // case 3
  if (a->import_star) {
    throw compile_error("import * is not supported", a->file_offset);

    // TODO: this isn't supported because we only allow importing immutable
    // globals from other modules in the other cases, and this would circumvent
    // that restriction and break guarantees
    const string& module_name = a->modules.begin()->first;
    auto module = this->global->get_module_at_phase(module_name,
        ModuleAnalysis::Phase::Analyzed);
    for (const auto& it : this->module->globals) {
      scope->at(it.first) = it.second;
    }
    return;
  }

  // case 1: import entire modules, not specific names
  if (a->names.empty()) {
    // we actually don't need to do anything here - AnnotationVisitor already
    // created the correct value type and linked it to the module object
    return;
  }

  // case 2: import some names from a module
  const string& module_name = a->modules.begin()->first;
  auto module = this->global->get_module_at_phase(module_name,
      ModuleAnalysis::Phase::Analyzed);
  for (const auto& it : a->names) {
    scope->at(it.second) = module->globals.at(it.first);
  }
}

void AnalysisVisitor::visit(GlobalStatement* a) {
  // nothing to do here; AnnotationVisitor already extracted all useful info
}

void AnalysisVisitor::visit(ExecStatement* a) {
  // we don't support this
  throw compile_error("ExecStatement is not supported", a->file_offset);
}

void AnalysisVisitor::visit(AssertStatement* a) {
  // run the check
  a->check->accept(this);

  // if we don't know what the check returned, assume we have to evaluate the
  // message
  if ((!this->current_value.value_known || !this->current_value.truth_value()) &&
      a->failure_message.get()) {
    a->failure_message->accept(this);
  }
}

void AnalysisVisitor::visit(BreakStatement* a) {
  // this is static analysis, not execution; we don't do anything here
}

void AnalysisVisitor::visit(ContinueStatement* a) {
  // this is static analysis, not execution; we don't do anything here
}

void AnalysisVisitor::visit(ReturnStatement* a) {
  // this tells us what the return type of the function is
  auto* context = this->current_function();
  if (!context) {
    throw compile_error("return statement outside function", a->file_offset);
  }

  if (a->value.get()) {
    a->value->accept(this);
    context->return_types.emplace(move(this->current_value));
  } else {
    context->return_types.emplace(ValueType::None);
  }
}

void AnalysisVisitor::visit(YieldStatement* a) {
  a->expr->accept(this);
}

void AnalysisVisitor::visit(SingleIfStatement* a) {
  throw logic_error("SingleIfStatement used instead of subclass");
}

void AnalysisVisitor::visit(IfStatement* a) {
  a->check->accept(this);

  // if we know the value and it's truthy, skip all the elif/else branches
  if (this->current_value.value_known && this->current_value.truth_value()) {
    this->visit_list(a->items);
    return;
  }

  // if we know the value and it's falsey, then skip this branch and advance
  // to the elifs
  // TODO: there may be more optimizations we can do here (e.g. if one of the
  // elifs is known and truthy, skip the rest and the else suite)
  if (this->current_value.value_known && !this->current_value.truth_value()) {
    for (auto& elif : a->elifs) {
      elif->accept(this);
    }
    if (a->else_suite.get()) {
      a->else_suite->accept(this);
    }
    return;
  }

  // we don't know the truth value of the condition; visit all branches
  this->visit_list(a->items);
  for (auto& elif : a->elifs) {
    elif->accept(this);
  }
  if (a->else_suite.get()) {
    a->else_suite->accept(this);
  }
}

void AnalysisVisitor::visit(ElseStatement* a) {
  this->visit_list(a->items);
}

void AnalysisVisitor::visit(ElifStatement* a) {
  a->check->accept(this);

  // if we don't know the value or it's truthy, skip this branch
  if (!this->current_value.value_known || this->current_value.truth_value()) {
    this->visit_list(a->items);
  }
}

void AnalysisVisitor::visit(ForStatement* a) {
  a->collection->accept(this);

  // if the current value is known, we can at least get the types of the values
  if (this->current_value.value_known) {
    switch (this->current_value.type) {
      // if we don't know the collection type, we can't know the value type;
      // just proceed without knowing
      case ValueType::Indeterminate:
        throw compile_error("encountered known value of Indeterminate type");

      // silly programmer; you can't iterate these types
      case ValueType::None:
      case ValueType::Bool:
      case ValueType::Int:
      case ValueType::Float:
      case ValueType::Function:
      case ValueType::Class:
      case ValueType::Module: {
        string target_value = this->current_value.str();
        throw compile_error(string_printf(
            "iteration target %s is not a collection", target_value.c_str()),
            a->file_offset);
      }

      // these you can iterate. if all the values are the same type, then we can
      // know what the result type is; otherwise, it's Indeterminate
      case ValueType::Bytes:
      case ValueType::Unicode:
        this->current_value = Variable(this->current_value.type);
        break;

      case ValueType::List:
      case ValueType::Tuple: {
        ValueType extension_type = ValueType::Indeterminate;
        for (const auto& item : *this->current_value.list_value) {
          // if we encounter a single Indeterminate item, the entire result is
          // Indeterminate
          if (item->type == ValueType::Indeterminate) {
            extension_type = ValueType::Indeterminate;
            break;

          // if we don't yet know the type, then record it
          } else if (extension_type == ValueType::Indeterminate) {
            extension_type = item->type;

          // if this item's type doesn't match the type we've found so far, the
          // entire result is Indeterminate
          } else if (extension_type != item->type) {
            extension_type = ValueType::Indeterminate;
            break;
          }
        }
        this->current_value = Variable(extension_type);
        break;
      }

      case ValueType::Set: {
        // same logic as for List/Tuple
        // TODO: deduplicate this
        ValueType extension_type = ValueType::Indeterminate;
        for (const auto& item : *this->current_value.set_value) {
          if (item.type == ValueType::Indeterminate) {
            extension_type = ValueType::Indeterminate;
            break;
          } else if (extension_type == ValueType::Indeterminate) {
            extension_type = item.type;
          } else if (extension_type != item.type) {
            extension_type = ValueType::Indeterminate;
            break;
          }
        }
        this->current_value = Variable(extension_type);
        break;
      }

      case ValueType::Dict: {
        // same logic as for List/Tuple, except just use the keys
        // TODO: deduplicate this
        ValueType extension_type = ValueType::Indeterminate;
        for (const auto& item : *this->current_value.dict_value) {
          if (item.first.type == ValueType::Indeterminate) {
            extension_type = ValueType::Indeterminate;
            break;
          } else if (extension_type == ValueType::Indeterminate) {
            extension_type = item.first.type;
          } else if (extension_type != item.first.type) {
            extension_type = ValueType::Indeterminate;
            break;
          }
        }
        this->current_value = Variable(extension_type);
        break;
      }
    }

  } else { // value not known
    switch (this->current_value.type) {
      // if we don't know the collection type, we can't know the value type;
      // just proceed without knowing
      case ValueType::Indeterminate:

      // for these we can't know what the result type will be without also
      // knowing the value
      case ValueType::List:
      case ValueType::Tuple:
      case ValueType::Set:
      case ValueType::Dict:
        this->current_value = Variable(ValueType::Indeterminate);
        break;

      // silly programmer; you can't iterate these types
      case ValueType::None:
      case ValueType::Bool:
      case ValueType::Int:
      case ValueType::Float:
      case ValueType::Function:
      case ValueType::Class:
      case ValueType::Module: {
        string target_type = this->current_value.str();
        throw compile_error(string_printf(
            "iteration target of type %s is not a collection", target_type.c_str()),
            a->file_offset);
      }

      // even if we don't know the value, we know what type the result will be
      case ValueType::Bytes:
      case ValueType::Unicode:
        this->current_value = Variable(this->current_value.type);
        break;
    }
  }

  a->variable->accept(this);

  this->visit_list(a->items);
  if (a->else_suite.get()) {
    a->else_suite->accept(this);
  }
}

void AnalysisVisitor::visit(WhileStatement* a) {
  a->condition->accept(this);
  this->visit_list(a->items);
  if (a->else_suite.get()) {
    a->else_suite->accept(this);
  }
}

void AnalysisVisitor::visit(ExceptStatement* a) {
  if (a->types.get()) {
    a->types->accept(this);
  }
  if (!a->name.empty()) {
    this->record_assignment(a->name, Variable(ValueType::Class), a->file_offset);
  }

  this->visit_list(a->items);
}

void AnalysisVisitor::visit(FinallyStatement* a) {
  this->visit_list(a->items);
}

void AnalysisVisitor::visit(TryStatement* a) {
  this->visit_list(a->items);

  for (auto& except : a->excepts) {
    except->accept(this); // lolz
  }
  if (a->else_suite.get()) {
    a->else_suite->accept(this);
  }
  if (a->finally_suite.get()) {
    a->finally_suite->accept(this);
  }
}

void AnalysisVisitor::visit(WithStatement* a) {
  for (auto& it : a->item_to_name) {
    it.first->accept(this);
    if (!it.second.empty()) {
      this->record_assignment(it.second, this->current_value, a->file_offset);
    }
  }
  this->visit_list(a->items);
}

void AnalysisVisitor::visit(FunctionDefinition* a) {
  // TODO: reduce code duplication between here and LambdaDefinition

  // assign all the arguments as Indeterminate for now; we'll come back and
  // fix them later

  if (!a->decorators.empty()) {
    throw compile_error("decorators not yet supported", a->file_offset);
  }

  int64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->function_id;
  auto* context = this->current_function();

  for (auto& arg : a->args.args) {
    // copy the argument definition into the function context
    context->args.emplace_back();
    auto& new_arg = context->args.back();
    new_arg.name = arg.name;
    if (arg.default_value.get()) {
      arg.default_value->accept(this);
      new_arg.default_value = move(this->current_value);
      if (new_arg.default_value.type == ValueType::Indeterminate) {
        throw compile_error("default value has Indeterminate type", a->file_offset);
      }
      if (!new_arg.default_value.value_known) {
        throw compile_error("can\'t resolve default value", a->file_offset);
      }
    }
  }
  context->varargs_name = a->args.varargs_name;
  context->varkwargs_name = a->args.varkwargs_name;

  this->visit_list(a->items);

  // if there's only one return type and it's None, delete it
  if ((context->return_types.size() == 1) && (context->return_types.begin()->type == ValueType::None)) {
    context->return_types.clear();
  }

  this->in_function_id = prev_function_id;

  this->record_assignment(a->name, Variable(a->function_id, false), a->file_offset);
}

void AnalysisVisitor::visit(ClassDefinition* a) {
  // TODO
  throw compile_error("ClassDefinition not yet implemented", a->file_offset);

  this->record_assignment(a->name, Variable(a->class_id, true), a->file_offset);
}

FunctionContext* AnalysisVisitor::current_function() {
  return this->global->context_for_function(this->in_function_id);
}

void AnalysisVisitor::record_assignment(const std::string& name,
    const Variable& var, size_t file_offset) {
  auto* context = this->current_function();
  bool is_global = !context || context->globals.count(name);
  bool is_mutable = !is_global || this->module->globals_mutable.at(name);

  if (is_global) {
    auto& global = this->module->globals.at(name);

    if (global.type == ValueType::Indeterminate) {
      global = var;

    // for mutable globals, we only keep track of the type and not the value;
    // for immutable globals, we keep track of both
    } else if (is_mutable) {
      if (global.type != var.type) {
        throw compile_error(string_printf(
            "global variable `%s` cannot change type", name.c_str()), file_offset);
      }
      global.clear_value();

    } else {
      throw compile_error(string_printf(
          "immutable global `%s` was written multiple times", name.c_str()), file_offset);
    }

  } else {
    // we keep track of the value only for the first write of a variable;
    // after that we keep track of the type only
    auto& function_locals = context->locals;
    try {
      auto& local_var = function_locals.at(name);
      if (local_var.type == ValueType::Indeterminate) {
        local_var = var; // this is the first set
      } else {
        if (local_var.type != var.type) {
          string existing_type = local_var.str();
          string new_type = var.str();
          throw compile_error(string_printf("%s changes type within function (from %s to %s)",
              name.c_str(), existing_type.c_str(), new_type.c_str()), file_offset);
        }

        // assume the value changed (this is not the first write)
        local_var.clear_value();
      }
    } catch (const out_of_range& e) {
      throw compile_error(string_printf(
          "local variable `%s` was not found in annotation phase", name.c_str()), file_offset);
    }
  }
}
