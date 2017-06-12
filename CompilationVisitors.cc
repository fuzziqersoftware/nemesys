#include "CompilationVisitors.hh"

#include <inttypes.h>
#include <stdio.h>

#include <phosg/Filesystem.hh>
#include <phosg/Strings.hh>

#include "PythonLexer.hh"
#include "PythonParser.hh"
#include "PythonASTNodes.hh"
#include "PythonASTVisitor.hh"
#include "Environment.hh"

using namespace std;



// compilation strategy (nemesys_compile_module):
// 1. load and parse source file
// 2. run AnnotationVisitor on it to get function IDs, variable names, and
//    imported module names
// 3. recursively load and parse source files and run AnnotationVisitor on them
//    for all imported modules
// 4. run AnalysisVisitor on the original source file
// TODO: the rest

// by the time 2 is done, we should be able to resolve all function IDs. by the
// time 4 is done, we should know the types of all variables (and in some cases,
// the values as well).



compile_error::compile_error(const std::string& what, ssize_t where) :
    runtime_error(where < 0 ? what : string_printf("%s (at %zd)", what.c_str(), where)),
    where(where) { }



ModuleAnalysis::ModuleAnalysis(const string& name, const string& filename) :
    phase(Phase::Initial), name(name), source(new SourceFile(filename)) { }

ModuleAnalysis::FunctionContext* ModuleAnalysis::context_for_function(
    uint64_t function_id) {
  if (function_id == 0) {
    return NULL;
  }
  return &this->function_id_to_context[function_id];
}



GlobalAnalysis::GlobalAnalysis() : import_paths({"."}) { }

void GlobalAnalysis::advance_module_phase(shared_ptr<ModuleAnalysis> module,
    ModuleAnalysis::Phase phase) {
  if (module->phase >= phase) {
    return;
  }

  // prevent infinite recursion: advance_phase_until cannot be called for a
  // module on which it is already executing (unless it would do nothing, above)
  if (!this->in_progress.emplace(module).second) {
    throw compile_error("cyclic import dependency");
  }

  while (module->phase < phase) {
    switch (module->phase) {
      case ModuleAnalysis::Phase::Initial: {
        shared_ptr<PythonLexer> lexer(new PythonLexer(module->source));
        if (this->debug_lexer) {
          fprintf(stderr, "[%s] lexer completed\n", module->name.c_str());
          const auto& tokens = lexer->get_tokens();
          for (size_t y = 0; y < tokens.size(); y++) {
            const auto& token = tokens[y];
            fprintf(stderr, "      n:%5lu type:%15s s:%s f:%lf i:%lld off:%lu len:%lu\n",
                y, PythonLexer::Token::name_for_token_type(token.type),
                token.string_data.c_str(), token.float_data, token.int_data,
                token.text_offset, token.text_length);
          }
          fputc('\n', stderr);
        }
        PythonParser parser(lexer);
        module->ast = parser.get_root();
        if (this->debug_parser) {
          fprintf(stderr, "[%s] parser completed\n", module->name.c_str());
          module->ast->print(stderr);
          fputc('\n', stderr);
        }
        module->phase = ModuleAnalysis::Phase::Parsed;
        break;
      }

      case ModuleAnalysis::Phase::Parsed: {
        AnnotationVisitor v(this, module.get());
        try {
          module->ast->accept(&v);
        } catch (const compile_error& e) {
          size_t line_num = module->source->line_number_of_offset(e.where);
          fprintf(stderr, "[%s] annotation failed at line %zu ($%zd): %s\n",
              module->name.c_str(), line_num, e.where, e.what());
          string line = module->source->line(line_num);
          fprintf(stderr, ">>> %s\n", line.c_str());
          fputs(">>> ", stderr);
          size_t space_count = e.where - module->source->line_offset(line_num);
          for (; space_count; space_count--) {
            fputc(' ', stderr);
          }
          fputs("^\n", stderr);
          throw;
        }
        if (this->debug_annotation) {
          fprintf(stderr, "[%s] annotation completed\n", module->name.c_str());
          module->ast->print(stderr);

          for (const auto& it : module->globals) {
            const char* mutable_str;
            try {
              mutable_str = module->globals_mutable.at(it.first) ? "mutable" : "immutable";
            } catch (const out_of_range& e) {
              mutable_str = "MISSING";
            }
            fprintf(stderr, "[%s] global: %s (%s)\n", module->name.c_str(),
                it.first.c_str(), mutable_str);
          }

          fprintf(stderr, "[%s] %zu functions declared\n", module->name.c_str(),
              module->function_id_to_context.size());
          for (const auto& it : module->function_id_to_context) {
            fprintf(stderr, "[%s] [%" PRIu64 "] %s\n", module->name.c_str(),
                it.first, it.second.name.c_str());
            for (const auto& global : it.second.globals) {
              fprintf(stderr, "[%s] [%" PRIu64 "] global: %s\n",
                  module->name.c_str(), it.first, global.c_str());
            }
            for (const auto& it2 : it.second.locals) {
              fprintf(stderr, "[%s] [%" PRIu64 "] local: %s\n",
                  module->name.c_str(), it.first, it2.first.c_str());
            }
          }
          fputc('\n', stderr);
        }
        module->phase = ModuleAnalysis::Phase::Annotated;
        break;
      }

      case ModuleAnalysis::Phase::Annotated: {
        AnalysisVisitor v(this, module.get());
        module->ast->accept(&v);
        if (this->debug_analysis) {
          fprintf(stderr, "[%s] analysis completed\n", module->name.c_str());

          for (const auto& it : module->globals) {
            bool is_mutable = module->globals_mutable.at(it.first);
            string value_str = it.second.str();
            fprintf(stderr, "[%s] global: %s = %s (%s)\n", module->name.c_str(),
                it.first.c_str(), value_str.c_str(),
                is_mutable ? "mutable" : "immutable");
          }

          for (const auto& it : module->function_id_to_context) {
            for (const auto& global : it.second.globals) {
              fprintf(stderr, "[%s] [%" PRIu64 "] global: %s\n",
                  module->name.c_str(), it.first, global.c_str());
            }
            for (const auto& it2 : it.second.locals) {
              string value_str = it2.second.str();
              fprintf(stderr, "[%s] [%" PRIu64 "] local: %s = %s\n",
                  module->name.c_str(), it.first, it2.first.c_str(),
                  value_str.c_str());
            }
            for (const auto& deleted : it.second.deleted_variables) {
              fprintf(stderr, "[%s] [%" PRIu64 "] deleted: %s\n",
                  module->name.c_str(), it.first, deleted.c_str());
            }
            for (const auto& type : it.second.return_types) {
              string type_str = type.str();
              fprintf(stderr, "[%s] [%" PRIu64 "] return type: %s\n",
                  module->name.c_str(), it.first, type_str.c_str());
            }
          }
          fputc('\n', stderr);
        }
        module->phase = ModuleAnalysis::Phase::Analyzed;
        break;
      }

      case ModuleAnalysis::Phase::Analyzed:
        break; // nothing to do
    }
  }

  this->in_progress.erase(module);
}

shared_ptr<ModuleAnalysis> GlobalAnalysis::get_module_at_phase(
    const string& module_name, ModuleAnalysis::Phase phase) {
  shared_ptr<ModuleAnalysis> module;
  try {
    module = this->modules.at(module_name);
  } catch (const out_of_range& e) {
    string filename = this->find_source_file(module_name);
    if (this->debug_find_file) {
      fprintf(stdout, "[%s] reading %s\n", module_name.c_str(), filename.c_str());
    }
    module = this->modules.emplace(piecewise_construct,
        forward_as_tuple(module_name),
        forward_as_tuple(new ModuleAnalysis(module_name, filename))).first->second;
    if (this->debug_source) {
      fprintf(stdout, "[%s] loaded source (%zu lines, %zu bytes)\n",
          module_name.c_str(), module->source->line_count(),
          module->source->file_size());
      fwrite(module->source->data().data(), module->source->file_size(), 1, stderr);
    }
  }

  this->advance_module_phase(module, phase);
  return module;
}

std::string GlobalAnalysis::find_source_file(const string& module_name) {
  // TODO: support dotted module names
  for (const string& path : this->import_paths) {
    string filename = path + "/" + module_name + ".py";
    try {
      stat(filename);
      return filename;
    } catch (const cannot_stat_file&) { }
  }

  throw compile_error("can\'t find file for module " + module_name);
}



AnnotationVisitor::AnnotationVisitor(GlobalAnalysis* global,
    ModuleAnalysis* module) : global(global), module(module),
    in_function_id(0), in_function_definition(false) { }

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
      if (!scope->emplace(it.second, Variable(it.first, true)).second) {
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
    context->globals.emplace(name);
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

void AnnotationVisitor::visit(ArgumentDefinition* a) {
  if (this->in_function_definition) {
    this->record_write(a->name, a->file_offset);
  }
  this->RecursiveASTVisitor::visit(a);
}

void AnnotationVisitor::visit(FunctionDefinition* a) {
  visit_list(a->decorators);

  a->function_id = this->next_function_id++;

  uint64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->function_id;

  auto* context = this->current_function();
  context->is_class = false;
  context->name = a->name;

  bool prev_in_function_definition = this->in_function_definition;
  this->in_function_definition = true;
  this->visit_list(a->args);
  this->in_function_definition = prev_in_function_definition;

  this->visit_list(a->items);
  this->in_function_id = prev_function_id;

  this->record_write(a->name, a->file_offset);
}

void AnnotationVisitor::visit(LambdaDefinition* a) {
  a->function_id = this->next_function_id++;

  uint64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->function_id;

  auto* context = this->current_function();
  context->is_class = false;
  context->name = string_printf("Lambda@%s$%zu+%" PRIu64,
      this->module->name.c_str(), a->file_offset, a->function_id);

  bool prev_in_function_definition = this->in_function_definition;
  this->in_function_definition = true;
  this->visit_list(a->args);
  this->in_function_definition = prev_in_function_definition;

  a->result->accept(this);

  this->in_function_id = prev_function_id;
}

void AnnotationVisitor::visit(ClassDefinition* a) {
  a->class_id = this->next_function_id++;

  uint64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->class_id;
  auto* context = this->current_function();
  context->is_class = true;
  context->name = a->name;
  this->RecursiveASTVisitor::visit(a);
  this->in_function_id = prev_function_id;

  this->record_write(a->name, a->file_offset);
}

atomic<uint64_t> AnnotationVisitor::next_function_id(1);

ModuleAnalysis::FunctionContext* AnnotationVisitor::current_function() {
  return this->module->context_for_function(this->in_function_id);
}

void AnnotationVisitor::record_write(const string& name, size_t file_offset) {
  if (name.empty()) {
    throw compile_error("empty name in record_write", file_offset);
  }

  auto* context = this->current_function();
  if (context) {
    if (context->globals.count(name)) {
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



AnalysisVisitor::AnalysisVisitor(GlobalAnalysis* global, ModuleAnalysis* module)
    : global(global), module(module), in_function_id(0) { }

void AnalysisVisitor::visit(UnaryOperation* a) {
  a->expr->accept(this);
  this->current_value = execute_unary_operator(a->oper, this->current_value);
}

void AnalysisVisitor::visit(BinaryOperation* a) {
  a->left->accept(this);
  Variable left = move(this->current_value);

  a->right->accept(this);

  this->current_value = execute_binary_operator(a->oper, left,
      this->current_value);
}

void AnalysisVisitor::visit(TernaryOperation* a) {
  a->left->accept(this);
  Variable left = move(this->current_value);

  a->center->accept(this);
  Variable center = move(this->current_value);

  a->right->accept(this);

  this->current_value = execute_ternary_operator(a->oper, left, center,
      this->current_value);
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
  // this is hard because we don't know the argument types; we need to see the
  // callsites before we can figure them out
  // TODO
  throw compile_error("lambdas currently are not supported", a->file_offset);
}

void AnalysisVisitor::visit(FunctionCall* a) {
  // the function reference had better be a function
  a->function->accept(this);
  if (this->current_value.type != ValueType::Function) {
    throw compile_error("cannot call a non-function object", a->file_offset);
  }

  // now look up the return type in the function registry, recurring if
  // necessary

  // TODO
  throw compile_error("function calls are currently not supported", a->file_offset);
}

void AnalysisVisitor::visit(ArrayIndex* a) {
  // TODO
  throw compile_error("array indexes are currently not supported", a->file_offset);
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
  if (this->in_function_id) {
    auto* context = this->current_function();
    if (context->globals.count(a->name)) {
      this->current_value = this->module->globals.at(a->name);
    } else {
      this->current_value = context->locals.at(a->name);
    }
  } else {
    // all lookups outside of a function are globals
    this->current_value = this->module->globals.at(a->name);
  }
}

void AnalysisVisitor::visit(AttributeLookup* a) {
  a->base->accept(this);

  // TODO: most of these have legitimate attributes that we should support
  switch (this->current_value.type) {
    case ValueType::Indeterminate:
      throw compile_error("attribute lookup on Indeterminate variable", a->file_offset);
    case ValueType::None:
      throw compile_error("attribute lookup on None value", a->file_offset);
    case ValueType::Bool:
      throw compile_error("attribute lookup on Bool value", a->file_offset);
    case ValueType::Int:
      throw compile_error("attribute lookup on Int value", a->file_offset);
    case ValueType::Float:
      throw compile_error("attribute lookup on Float value", a->file_offset);
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
    case ValueType::Function:
      throw compile_error("attribute lookup on Function value", a->file_offset);
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
  throw compile_error("lvalue reference writes to array index", a->file_offset);
}

void AnalysisVisitor::visit(ArraySliceLValueReference* a) {
  throw compile_error("lvalue reference writes to array slice", a->file_offset);
}

void AnalysisVisitor::visit(AttributeLValueReference* a) {
  this->record_assignment(a->name, this->current_value, a->file_offset);
}

void AnalysisVisitor::visit(ArgumentDefinition* a) {
  // TODO
  throw runtime_error("ArgumentDefinition not yet implemented");
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
  throw runtime_error("AugmentStatement not yet implemented");
}

void AnalysisVisitor::visit(DeleteStatement* a) {
  if (this->in_function_id) {
    // TODO
    throw runtime_error("DeleteStatement not yet implemented");
  } else {
    // TODO: do we need to support this? seems unlikely
    throw invalid_argument("DeleteStatement only supported in functions");
  }
}

void AnalysisVisitor::visit(ImportStatement* a) {
  // this is similar to AnnotationVisitor, except we copy values too, and we
  // expect all the names to already exist in the target scope

  auto* context = this->current_function();
  auto* scope = context ? &context->locals : &this->module->globals;

  // case 3
  if (a->import_star) {
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
  if (!this->current_value.value_known || !this->current_value.truth_value()) {
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
    context->return_types.emplace(new Variable(move(this->current_value)));
  } else {
    context->return_types.emplace(new Variable(ValueType::None));
  }
}

void AnalysisVisitor::visit(RaiseStatement* a) {
  // TODO
  throw runtime_error("RaiseStatement not yet implemented");
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

  // TODO: we're going to have to fix this; it won't pick up the right type from
  // the collection
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
  a->types->accept(this);
  this->record_assignment(a->name, Variable(ValueType::Class), a->file_offset);

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
  // assign all the arguments as Indeterminate for now; we'll come back and
  // fix them later

  if (!a->decorators.empty()) {
    throw runtime_error("decorators not yet supported");
  }

  uint64_t prev_function_id = this->in_function_id;
  this->in_function_id = a->function_id;

  for (auto& arg : a->args) {
    arg->accept(this);
  }
  this->visit_list(a->items);

  // if there's only one return type and it's None, delete it
  auto* context = this->current_function();
  if ((context->return_types.size() == 1) && (context->return_types.begin()->type == ValueType::None)) {
    context->return_types.clear();
  }

  this->in_function_id = prev_function_id;

  this->record_assignment(a->name, Variable(a->function_id, false), a->file_offset);
}

void AnalysisVisitor::visit(ClassDefinition* a) {
  // TODO
  throw runtime_error("ClassDefinition not yet implemented");

  this->record_assignment(a->name, Variable(a->class_id, true), a->file_offset);
}

ModuleAnalysis::FunctionContext* AnalysisVisitor::current_function() {
  return this->module->context_for_function(this->in_function_id);
}

void AnalysisVisitor::record_assignment(const std::string& name,
    const Variable& var, size_t file_offset) {
  bool global_mutable;
  bool is_global = true;
  try {
    if (this->in_function_id) {
      if (this->current_function()->globals.count(name)) {
        global_mutable = this->module->globals_mutable.at(name);
      } else {
        is_global = false;
      }
    } else {
      global_mutable = this->module->globals_mutable.at(name);
    }
  } catch (const out_of_range& e) {
    throw compile_error("invalid function id or global name", file_offset);
  }

  if (is_global) {
    // for mutable globals, we only keep track of the type and not the value;
    // for immutable globals, we keep track of both
    if (global_mutable) {
      auto& global = this->module->globals[name];
      if (global.type == ValueType::Indeterminate) {
        global = var;
      } else if (global.type != var.type) {
        throw compile_error(string_printf(
            "global variable `%s` cannot change type", name.c_str()), file_offset);
      }
    } else {
      if (!this->module->globals.emplace(name, var).second) {
        throw compile_error(string_printf(
            "immutable global `%s` was written multiple times", name.c_str()), file_offset);
      }
    }

  } else {
    // we keep track of the value only for the first write of a variable;
    // after that we keep track of the type only
    auto& function_locals = this->current_function()->locals;
    try {
      auto& local_var = function_locals.at(name);
      if (local_var.type == ValueType::Indeterminate) {
        local_var = var; // this is the first set
      } else {
        if (local_var.type != var.type) {
          throw compile_error(name + " changes type within function", file_offset);
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
