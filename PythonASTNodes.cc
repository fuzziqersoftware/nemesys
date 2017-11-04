#include <inttypes.h>
#include <stdio.h>

#include <memory>
#include <phosg/Strings.hh>
#include <string>
#include <vector>

#include "PythonLexer.hh" // for escape
#include "PythonASTNodes.hh"
#include "PythonASTVisitor.hh"

using namespace std;



// helpers for str()/print() methods

template<typename T>
static string comma_str_list(const vector<shared_ptr<T>>& l) {
  string ret;
  for (int x = 0; x < l.size(); x++) {
    if (x > 0) {
      ret += ", ";
    }
    if (!l[x]) {
      ret += "NULL";
    } else {
      ret += l[x]->str();
    }
  }
  return ret;
}

template<typename T>
static string comma_str_list(const vector<T>& l) {
  string ret;
  for (int x = 0; x < l.size(); x++) {
    if (x > 0) {
      ret += ", ";
    }
    ret += l[x].str();
  }
  return ret;
}

static string comma_list(const vector<string>& l) {
  string ret;
  for (int x = 0; x < l.size(); x++) {
    if (x > 0) {
      ret += ", ";
    }
    ret += l[x];
  }
  return ret;
}

template<typename T>
static string str_or_null(const shared_ptr<T> item) {
  return item.get() ? item->str() : "NULL";
}

const char* augment_operator_names[] = {
  "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "**=", "//=", NULL};



// non-member functions

BinaryOperator binary_operator_for_augment_operator(AugmentOperator oper) {
  static const unordered_map<AugmentOperator, BinaryOperator> m({
    {AugmentOperator::Addition, BinaryOperator::Addition},
    {AugmentOperator::Subtraction, BinaryOperator::Subtraction},
    {AugmentOperator::Multiplication, BinaryOperator::Multiplication},
    {AugmentOperator::Division, BinaryOperator::Division},
    {AugmentOperator::Modulus, BinaryOperator::Modulus},
    {AugmentOperator::And, BinaryOperator::And},
    {AugmentOperator::Or, BinaryOperator::Or},
    {AugmentOperator::Xor, BinaryOperator::Xor},
    {AugmentOperator::LeftShift, BinaryOperator::LeftShift},
    {AugmentOperator::RightShift, BinaryOperator::RightShift},
    {AugmentOperator::Exponentiation, BinaryOperator::Exponentiation},
    {AugmentOperator::IntegerDivision, BinaryOperator::IntegerDivision},
  });
  return m.at(oper);
}



ASTNode::ASTNode(size_t file_offset) : file_offset(file_offset) { }



Expression::Expression(size_t file_offset) : ASTNode(file_offset) { }

bool Expression::valid_lvalue() const {
  return false;
}



LValueReference::LValueReference(size_t file_offset) :
    Expression(file_offset) { }

bool LValueReference::valid_lvalue() const {
  return true;
}



AttributeLValueReference::AttributeLValueReference(shared_ptr<Expression> base,
    const string& name, std::shared_ptr<TypeAnnotation> type_annotation,
    size_t file_offset) : LValueReference(file_offset), base(base), name(name),
    type_annotation(type_annotation) { }

string AttributeLValueReference::str() const {
  // TODO: add type annotations
  if (this->base.get()) {
    return this->base->str() + "." + this->name + " /*lv*/";
  }
  return this->name + " /*lv*/";
}

void AttributeLValueReference::accept(ASTVisitor* v) {
  v->visit(this);
}



ArrayIndexLValueReference::ArrayIndexLValueReference(
    shared_ptr<Expression> array, shared_ptr<Expression> index,
    size_t file_offset) : LValueReference(file_offset), array(array),
    index(index) { }

string ArrayIndexLValueReference::str() const {
  return this->array->str() + "[" + this->index->str() + "] /*lv*/";
}

void ArrayIndexLValueReference::accept(ASTVisitor* v) {
  v->visit(this);
}



ArraySliceLValueReference::ArraySliceLValueReference(
    shared_ptr<Expression> array, shared_ptr<Expression> start_index,
    shared_ptr<Expression> end_index, shared_ptr<Expression> step_size,
    size_t file_offset) : LValueReference(file_offset), array(array),
    start_index(start_index), end_index(end_index), step_size(step_size) { }

string ArraySliceLValueReference::str() const {
  string ret = this->array->str() + "[";
  if (this->start_index.get()) {
    ret += this->start_index->str();
  }
  ret += ":";
  if (this->end_index.get()) {
    ret += this->end_index->str();
  }
  if (this->step_size.get()) {
    ret += ":";
    ret += this->step_size->str();
  }
  ret += "] /*lv*/";
  return ret;
}

void ArraySliceLValueReference::accept(ASTVisitor* v) {
  v->visit(this);
}



TupleLValueReference::TupleLValueReference(
    vector<shared_ptr<Expression>>&& items, size_t file_offset) :
    LValueReference(file_offset), items(move(items)) { }

bool TupleLValueReference::valid_lvalue() const {
  for (const auto& item : this->items) {
    if (!item->valid_lvalue()) {
      return false;
    }
  }
  return true;
}

string TupleLValueReference::str() const {
  return comma_str_list(this->items);
}

void TupleLValueReference::accept(ASTVisitor* v) {
  v->visit(this);
}



UnaryOperation::UnaryOperation(UnaryOperator oper, shared_ptr<Expression> expr,
    size_t file_offset) : Expression(file_offset), oper(oper), expr(expr),
    split_id(0) { }

static const char* unary_operator_names[] = {
  "not ",
  "~",
  "+",
  "-",
  "$REPR$", // special-cased in str()
  "yield ",
  "$INVALID$",
};

string UnaryOperation::str() const {
  auto expr_str = this->expr->str();
  if (this->oper == UnaryOperator::Yield) {
    string split_id_str = this->split_id ? string_printf("/*split=%" PRIu64 "*/ ", this->split_id) : "";
    return string_printf("(yield %s%s)", split_id_str.c_str(), expr_str.c_str());
  }
  return string_printf("(%s%s)",
      unary_operator_names[static_cast<size_t>(this->oper)], expr_str.c_str());
}

void UnaryOperation::accept(ASTVisitor* v) {
  v->visit(this);
}



BinaryOperation::BinaryOperation(BinaryOperator oper,
    shared_ptr<Expression> left, shared_ptr<Expression> right,
    size_t file_offset) : Expression(file_offset), oper(oper), left(left),
    right(right) { }

static const char* binary_operator_names[] = {
  "or",
  "and",
  "<",
  ">",
  "==",
  ">=",
  "<=",
  "!=",
  "in",
  "not in",
  "is",
  "is not",
  "|",
  "&",
  "^",
  "<<",
  ">>",
  "+",
  "-",
  "*",
  "/",
  "%",
  "//",
  "**",
  "$INVALID$",
};

string BinaryOperation::str() const {
  string left_str = this->left->str();
  string right_str = this->right->str();
  return string_printf("(%s %s %s)", left_str.c_str(),
      binary_operator_names[static_cast<size_t>(this->oper)], right_str.c_str());
}

void BinaryOperation::accept(ASTVisitor* v) {
  v->visit(this);
}



TernaryOperation::TernaryOperation(TernaryOperator oper,
    shared_ptr<Expression> left, shared_ptr<Expression> center,
    shared_ptr<Expression> right, size_t file_offset) : Expression(file_offset),
    oper(oper), left(left), center(center), right(right) { }

string TernaryOperation::str() const {
  string left_str = this->left->str();
  string center_str = this->center->str();
  string right_str = this->right->str();
  return string_printf("(%s if %s else %s)", left_str.c_str(),
      center_str.c_str(), right_str.c_str());
}

void TernaryOperation::accept(ASTVisitor* v) {
  v->visit(this);
}



ListConstructor::ListConstructor(size_t file_offset) :
    Expression(file_offset) { }

ListConstructor::ListConstructor(vector<shared_ptr<Expression>>&& items,
    size_t file_offset) : Expression(file_offset), items(move(items)) { }

string ListConstructor::str() const {
  return "[" + comma_str_list(this->items) + "]";
}

void ListConstructor::accept(ASTVisitor* v) {
  v->visit(this);
}



DictConstructor::DictConstructor(size_t file_offset) :
    Expression(file_offset) { }

DictConstructor::DictConstructor(
    vector<pair<shared_ptr<Expression>, shared_ptr<Expression>>>&& items,
    size_t file_offset) : Expression(file_offset), items(move(items)) { }

string DictConstructor::str() const {
  string ret;
  for (const auto& it : this->items) {
    if (!ret.empty()) {
      ret += ", ";
    }
    ret += it.first->str();
    ret += ": ";
    ret += it.second->str();
  }
  return "{" + ret + "}";
}

void DictConstructor::accept(ASTVisitor* v) {
  v->visit(this);
}



SetConstructor::SetConstructor(vector<shared_ptr<Expression>>&& items,
    size_t file_offset) : Expression(file_offset), items(move(items)) { }

string SetConstructor::str() const {
  return "set(" + comma_str_list(this->items) + ")";
}

void SetConstructor::accept(ASTVisitor* v) {
  v->visit(this);
}



TupleConstructor::TupleConstructor(size_t file_offset) :
    Expression(file_offset) { }

TupleConstructor::TupleConstructor(vector<shared_ptr<Expression>>&& items,
    size_t file_offset) : Expression(file_offset), items(move(items)) { }

string TupleConstructor::str() const {
  return "(" + comma_str_list(this->items) + ")";
}

void TupleConstructor::accept(ASTVisitor* v) {
  v->visit(this);
}



ListComprehension::ListComprehension(shared_ptr<Expression> item_pattern,
    shared_ptr<Expression> variable, shared_ptr<Expression> source_data,
    shared_ptr<Expression> predicate, size_t file_offset) :
    Expression(file_offset), item_pattern(item_pattern), variable(variable),
    source_data(source_data), predicate(predicate) { }

string ListComprehension::str() const {
  string item_str = this->item_pattern->str();
  string vars_str = this->variable->str();
  string source_str = this->source_data->str();
  if (!this->predicate) {
    return string_printf("[%s for %s in %s]", item_str.c_str(),
        vars_str.c_str(), source_str.c_str());
  }
  string pred_str = this->predicate->str();
  return string_printf("[%s for %s in %s if %s]", item_str.c_str(),
      vars_str.c_str(), source_str.c_str(), pred_str.c_str());
}

void ListComprehension::accept(ASTVisitor* v) {
  v->visit(this);
}



DictComprehension::DictComprehension(shared_ptr<Expression> key_pattern,
    shared_ptr<Expression> value_pattern, shared_ptr<Expression> variable,
    shared_ptr<Expression> source_data, shared_ptr<Expression> predicate,
    size_t file_offset) : Expression(file_offset), key_pattern(key_pattern),
    value_pattern(value_pattern), variable(variable), source_data(source_data),
    predicate(predicate) { }

string DictComprehension::str() const {
  string key_str = this->key_pattern->str();
  string value_str = this->value_pattern->str();
  string vars_str = this->variable->str();
  string source_str = this->source_data->str();
  if (!this->predicate) {
    return string_printf("{%s: %s for %s in %s}", key_str.c_str(),
        value_str.c_str(), vars_str.c_str(), source_str.c_str());
  }
  string pred_str = this->predicate->str();
  return string_printf("{%s: %s for %s in %s if %s}", key_str.c_str(),
      value_str.c_str(), vars_str.c_str(), source_str.c_str(),
      pred_str.c_str());
}

void DictComprehension::accept(ASTVisitor* v) {
  v->visit(this);
}



SetComprehension::SetComprehension(shared_ptr<Expression> item_pattern,
    shared_ptr<Expression> variable, shared_ptr<Expression> source_data,
    shared_ptr<Expression> predicate, size_t file_offset) :
    Expression(file_offset), item_pattern(item_pattern), variable(variable),
    source_data(source_data), predicate(predicate) { }

string SetComprehension::str() const {
  string item_str = this->item_pattern->str();
  string vars_str = this->variable->str();
  string source_str = this->source_data->str();
  if (!this->predicate) {
    return string_printf("{%s for %s in %s}", item_str.c_str(),
        vars_str.c_str(), source_str.c_str());
  }
  string pred_str = this->predicate->str();
  return string_printf("{%s for %s in %s if %s}", item_str.c_str(),
      vars_str.c_str(), source_str.c_str(), pred_str.c_str());
}

void SetComprehension::accept(ASTVisitor* v) {
  v->visit(this);
}



FunctionArguments::Argument::Argument(const string& name,
    std::shared_ptr<TypeAnnotation> type_annotation,
    std::shared_ptr<Expression> default_value) : name(name),
    type_annotation(type_annotation), default_value(default_value) { }

string FunctionArguments::Argument::str() const {
  // TODO: add type annotations here
  if (this->default_value.get()) {
    return this->name + "=" + this->default_value->str();
  }
  return this->name;
}




FunctionArguments::FunctionArguments(std::vector<Argument>&& args,
    const std::string& varargs_name, const std::string& varkwargs_name) :
    args(move(args)), varargs_name(varargs_name),
    varkwargs_name(varkwargs_name) { }

string FunctionArguments::str() const {
  string ret = comma_str_list(this->args);
  if (!this->varargs_name.empty()) {
    if (!ret.empty()) {
      ret += ", ";
    }
    ret += '*';
    ret += this->varargs_name;
  }
  if (!this->varkwargs_name.empty()) {
    if (!ret.empty()) {
      ret += ", ";
    }
    ret += "**";
    ret += this->varkwargs_name;
  }
  return ret;
}




LambdaDefinition::LambdaDefinition(FunctionArguments&& args,
    shared_ptr<Expression> result, size_t file_offset) :
    Expression(file_offset), args(move(args)), result(result) { }

string LambdaDefinition::str() const {
  return "lambda " + this->args.str() + ": " + this->result->str();
}

void LambdaDefinition::accept(ASTVisitor* v) {
  v->visit(this);
}



FunctionCall::FunctionCall(shared_ptr<Expression> function,
    vector<shared_ptr<Expression>>&& args,
    unordered_map<string, shared_ptr<Expression>>&& kwargs,
    shared_ptr<Expression> varargs, shared_ptr<Expression> varkwargs,
    size_t file_offset) : Expression(file_offset), function(function),
    args(move(args)), kwargs(move(kwargs)), varargs(varargs),
    varkwargs(varkwargs), function_id(0), split_id(0), callee_function_id(0) { }

string FunctionCall::str() const {
  string split_id_str = this->split_id ?
      string_printf("/*split=%" PRId64 "*/", this->split_id) : "";
  string callee_id_str = this->callee_function_id ?
      string_printf("/*callee=%" PRId64 "*/", this->callee_function_id) : "";
  return this->function->str() + split_id_str + callee_id_str + "(" + comma_str_list(this->args) + ")";
}

void FunctionCall::accept(ASTVisitor* v) {
  v->visit(this);
}



ArrayIndex::ArrayIndex(shared_ptr<Expression> array,
    shared_ptr<Expression> index, size_t file_offset) : Expression(file_offset),
    array(array), index(index), index_constant(false), index_value(0) { }

string ArrayIndex::str() const {
  return this->array->str() + "[" + this->index->str() + "]";
}

void ArrayIndex::accept(ASTVisitor* v) {
  v->visit(this);
}



ArraySlice::ArraySlice(shared_ptr<Expression> array,
    shared_ptr<Expression> start_index, shared_ptr<Expression> end_index,
    shared_ptr<Expression> step_size, size_t file_offset) :
    Expression(file_offset), array(array), start_index(start_index),
    end_index(end_index), step_size(step_size) { }

string ArraySlice::str() const {
  string ret = this->array->str() + "[";
  if (this->start_index.get()) {
    ret += this->start_index->str();
  }
  ret += ":";
  if (this->end_index.get()) {
    ret += this->end_index->str();
  }
  if (this->step_size.get()) {
    ret += ":";
    ret += this->step_size->str();
  }
  ret += "]";
  return ret;
}

void ArraySlice::accept(ASTVisitor* v) {
  v->visit(this);
}



IntegerConstant::IntegerConstant(int64_t value, size_t file_offset) :
    Expression(file_offset), value(value) { }

string IntegerConstant::str() const {
  return to_string(this->value);
}

void IntegerConstant::accept(ASTVisitor* v) {
  v->visit(this);
}



FloatConstant::FloatConstant(double value, size_t file_offset) :
    Expression(file_offset), value(value) { }

string FloatConstant::str() const {
  return to_string(this->value);
}

void FloatConstant::accept(ASTVisitor* v) {
  v->visit(this);
}



BytesConstant::BytesConstant(const string& value, size_t file_offset) :
    Expression(file_offset), value(value) { }

string BytesConstant::str() const {
  return "b\'" + escape(this->value) + "\'";
}

void BytesConstant::accept(ASTVisitor* v) {
  v->visit(this);
}



UnicodeConstant::UnicodeConstant(const wstring& value, size_t file_offset) :
    Expression(file_offset), value(value) { }

string UnicodeConstant::str() const {
  return "u\'" + escape(this->value) + "\'";
}

void UnicodeConstant::accept(ASTVisitor* v) {
  v->visit(this);
}



TrueConstant::TrueConstant(size_t file_offset) : Expression(file_offset) { }

string TrueConstant::str() const {
  return "True";
}

void TrueConstant::accept(ASTVisitor* v) {
  v->visit(this);
}



FalseConstant::FalseConstant(size_t file_offset) : Expression(file_offset) { }

string FalseConstant::str() const {
  return "False";
}

void FalseConstant::accept(ASTVisitor* v) {
  v->visit(this);
}



NoneConstant::NoneConstant(size_t file_offset) : Expression(file_offset) { }

string NoneConstant::str() const {
  return "None";
}

void NoneConstant::accept(ASTVisitor* v) {
  v->visit(this);
}



VariableLookup::VariableLookup(const string& name, size_t file_offset) :
    Expression(file_offset), name(name) { }

string VariableLookup::str() const {
  return this->name;
}

void VariableLookup::accept(ASTVisitor* v) {
  v->visit(this);
}



AttributeLookup::AttributeLookup(shared_ptr<Expression> base,
    const string& name, size_t file_offset) : Expression(file_offset),
    base(base), name(name) { }

string AttributeLookup::str() const {
  return this->base->str() + "." + this->name;
}

void AttributeLookup::accept(ASTVisitor* v) {
  v->visit(this);
}



Statement::Statement(size_t file_offset) : ASTNode(file_offset) { }



SimpleStatement::SimpleStatement(size_t file_offset) :
    Statement(file_offset) { }

void SimpleStatement::print(FILE* stream, size_t indent_level) const {
  print_indent(stream, indent_level);
  string s = this->str();
  fprintf(stream, "%s\n", s.c_str());
}



CompoundStatement::CompoundStatement(vector<shared_ptr<Statement>>&& items,
    size_t file_offset) : Statement(file_offset), items(move(items)) { }

void CompoundStatement::print(FILE* stream, size_t indent_level) const {
  print_indent(stream, indent_level);
  string s = this->str();
  fprintf(stream, "%s\n", s.c_str());
  for (const auto& it : this->items) {
    if (!it) {
      fprintf(stream, "# NULL STATEMENT\n");
    } else {
      it->print(stream, indent_level + 2);
    }
  }
}



ModuleStatement::ModuleStatement(vector<shared_ptr<Statement>>&& items,
    size_t file_offset) : CompoundStatement(move(items), file_offset) { }

string ModuleStatement::str() const {
  return "# ModuleStatement";
}

void ModuleStatement::print(FILE* stream, size_t indent_level) const {
  // basically the same as the base class method except it doesn't indent
  print_indent(stream, indent_level);
  string s = this->str();
  fprintf(stream, "%s\n", s.c_str());
  for (const auto& it : this->items) {
    if (!it) {
      fprintf(stream, "# NULL STATEMENT\n");
    } else {
      it->print(stream, indent_level);
    }
  }
}

void ModuleStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



ExpressionStatement::ExpressionStatement(shared_ptr<Expression> expr,
    size_t file_offset) : SimpleStatement(file_offset), expr(expr) { }

string ExpressionStatement::str() const {
  return this->expr->str();
}

void ExpressionStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



AssignmentStatement::AssignmentStatement(shared_ptr<Expression> target,
    shared_ptr<Expression> value, size_t file_offset) :
    SimpleStatement(file_offset), target(target), value(value) { }

string AssignmentStatement::str() const {
  return this->target->str() + " = " + this->value->str();
}

void AssignmentStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



AugmentStatement::AugmentStatement(AugmentOperator oper,
    shared_ptr<Expression> target, shared_ptr<Expression> value,
    size_t file_offset) : SimpleStatement(file_offset), oper(oper),
    target(target), value(value) { }

string AugmentStatement::str() const {
  return this->target->str() + " " +
      augment_operator_names[static_cast<size_t>(oper)] + " " +
      this->value->str();
}

void AugmentStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



DeleteStatement::DeleteStatement(shared_ptr<Expression> items,
    size_t file_offset) : SimpleStatement(file_offset), items(items) { }

string DeleteStatement::str() const {
  return "del " + this->items->str();
}

void DeleteStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



PassStatement::PassStatement(size_t file_offset) :
    SimpleStatement(file_offset) { }

string PassStatement::str() const {
  return "pass";
}

void PassStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



FlowStatement::FlowStatement(size_t file_offset) :
    SimpleStatement(file_offset) { }



ImportStatement::ImportStatement(unordered_map<string, string>&& modules,
    unordered_map<string, string>&& names, bool import_star, size_t file_offset)
    : SimpleStatement(file_offset), modules(move(modules)), names(move(names)),
    import_star(import_star) { }

string ImportStatement::str() const {
  // case 3 (from x import *)
  if (this->import_star) {
    return string_printf("from %s import *", this->modules.begin()->first.c_str());
  }

  // case 1 (import a as b, c as d, etc.)
  if (this->names.empty()) {
    string ret = "import ";
    for (const auto& it : this->modules) {
      if (ret.size() > 7) {
        ret += ", ";
      }

      if (it.second != it.first) {
        ret += string_printf("%s as %s", it.first.c_str(), it.second.c_str());
      } else {
        ret += it.first;
      }
    }
    return ret;
  }

  // case 2 (from a import b as c, d as e, ...)
  string ret = "from " + this->modules.begin()->first + " import ";
  size_t initial_size = ret.size();
  for (const auto& it : this->names) {
    if (ret.size() > initial_size) {
      ret += ", ";
    }

    if (it.second != it.first) {
      ret += string_printf("%s as %s", it.first.c_str(), it.second.c_str());
    } else {
      ret += it.first;
    }
  }
  return ret;
}

void ImportStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



GlobalStatement::GlobalStatement(vector<string>&& names, size_t file_offset) :
    SimpleStatement(file_offset), names(move(names)) { }

string GlobalStatement::str() const {
  return "global " + comma_list(this->names);
}

void GlobalStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



ExecStatement::ExecStatement(shared_ptr<Expression> code,
    shared_ptr<Expression> globals, shared_ptr<Expression> locals,
    size_t file_offset) : SimpleStatement(file_offset), code(code),
    globals(globals), locals(locals) { }

string ExecStatement::str() const {
  return "exec " + this->code->str() + ", " + str_or_null(this->globals) +
      ", " + str_or_null(this->locals);
}

void ExecStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



AssertStatement::AssertStatement(shared_ptr<Expression> check,
    shared_ptr<Expression> failure_message, size_t file_offset) :
    SimpleStatement(file_offset), check(check),
    failure_message(failure_message) { }

string AssertStatement::str() const {
  return "assert " + this->check->str() + ", " +
      str_or_null(this->failure_message);
}

void AssertStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



BreakStatement::BreakStatement(size_t file_offset) :
    FlowStatement(file_offset) { }

string BreakStatement::str() const {
  return "break";
}

void BreakStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



ContinueStatement::ContinueStatement(size_t file_offset) :
    FlowStatement(file_offset) { }

string ContinueStatement::str() const {
  return "continue";
}

void ContinueStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



ReturnStatement::ReturnStatement(shared_ptr<Expression> value,
    size_t file_offset) : FlowStatement(file_offset), value(value) { }

string ReturnStatement::str() const {
  return "return " + this->value->str();
}

void ReturnStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



RaiseStatement::RaiseStatement(shared_ptr<Expression> type,
    shared_ptr<Expression> value, shared_ptr<Expression> traceback,
    size_t file_offset) : FlowStatement(file_offset), type(type), value(value),
    traceback(traceback) { }

string RaiseStatement::str() const {
  return "raise " + str_or_null(this->type) + ", " + str_or_null(this->value) +
      ", " + str_or_null(this->traceback);
}

void RaiseStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



YieldStatement::YieldStatement(shared_ptr<Expression> expr, bool from,
    size_t file_offset) : FlowStatement(file_offset), expr(expr), from(from),
    split_id(0) { }

string YieldStatement::str() const {
  string prefix = "yield ";
  if (this->from) {
    prefix += "from ";
  }
  string split_id_str = this->split_id ? string_printf("/*split=%" PRIu64 "*/ ", this->split_id) : "";
  return prefix + split_id_str + str_or_null(this->expr);
}

void YieldStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



SingleIfStatement::SingleIfStatement(shared_ptr<Expression> check,
    vector<shared_ptr<Statement>>&& items, size_t file_offset) :
    CompoundStatement(move(items), file_offset), check(check),
    always_true(false), always_false(false) { }

string SingleIfStatement::str() const {
  return "if " + this->check->str() + ":";
}

void SingleIfStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



ElseStatement::ElseStatement(vector<shared_ptr<Statement>>&& items,
    size_t file_offset) : CompoundStatement(move(items), file_offset) { }

string ElseStatement::str() const {
  return "else:";
};

void ElseStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



ElifStatement::ElifStatement(shared_ptr<Expression> check,
    vector<shared_ptr<Statement>>&& items, size_t file_offset) :
    SingleIfStatement(check, move(items), file_offset) { }

string ElifStatement::str() const {
  return "elif " + this->check->str() + ":";
}

void ElifStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



IfStatement::IfStatement(shared_ptr<Expression> check,
    vector<shared_ptr<Statement>>&& items,
    vector<shared_ptr<ElifStatement>>&& elifs,
    shared_ptr<ElseStatement> else_suite, size_t file_offset) :
    SingleIfStatement(check, move(items), file_offset), elifs(move(elifs)),
    else_suite(else_suite) { }

string IfStatement::str() const {
  return "if " + this->check->str() + ":";
}

void IfStatement::print(FILE* stream, size_t indent_level) const {
  CompoundStatement::print(stream, indent_level);
  for (const auto& it : this->elifs) {
    if (!it) {
      fprintf(stream, "# NULL STATEMENT\n");
    } else {
      it->print(stream, indent_level);
    }
  }
  if (this->else_suite) {
    this->else_suite->print(stream, indent_level);
  }
}

void IfStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



ForStatement::ForStatement(shared_ptr<Expression> variable,
    shared_ptr<Expression> collection, vector<shared_ptr<Statement>>&& items,
    shared_ptr<ElseStatement> else_suite, size_t file_offset) :
    CompoundStatement(move(items), file_offset), variable(variable),
    collection(collection) { }

string ForStatement::str() const {
  return "for " + this->variable->str() + " in " + this->collection->str() + ":";
}

void ForStatement::print(FILE* stream, size_t indent_level) const {
  CompoundStatement::print(stream, indent_level);
  if (this->else_suite) {
    this->else_suite->print(stream, indent_level);
  }
}

void ForStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



WhileStatement::WhileStatement(shared_ptr<Expression> condition,
    vector<shared_ptr<Statement>>&& items, shared_ptr<ElseStatement> else_suite,
    size_t file_offset) : CompoundStatement(move(items), file_offset),
    condition(condition), else_suite(else_suite) { }

string WhileStatement::str() const {
  return "while " + this->condition->str() + ":";
}

void WhileStatement::print(FILE* stream, size_t indent_level) const {
  CompoundStatement::print(stream, indent_level);
  if (this->else_suite) {
    this->else_suite->print(stream, indent_level);
  }
}

void WhileStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



ExceptStatement::ExceptStatement(shared_ptr<Expression> types,
    const string& name, vector<shared_ptr<Statement>>&& items,
    size_t file_offset) : CompoundStatement(move(items), file_offset),
    types(types), name(name) { }

string ExceptStatement::str() const {
  if (!this->types) {
    return "except:";
  }
  if (!this->name.length()) {
    return "except " + this->types->str() + ":";
  }
  return "except " + this->types->str() + " as " + this->name + ":";
}

void ExceptStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



FinallyStatement::FinallyStatement(vector<shared_ptr<Statement>>&& items,
    size_t file_offset) : CompoundStatement(move(items), file_offset) { }

string FinallyStatement::str() const {
  return "finally:";
};

void FinallyStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



TryStatement::TryStatement(vector<shared_ptr<Statement>>&& items,
    vector<shared_ptr<ExceptStatement>>&& excepts,
    shared_ptr<ElseStatement> else_suite,
    shared_ptr<FinallyStatement> finally_suite, size_t file_offset) :
    CompoundStatement(move(items), file_offset), excepts(move(excepts)),
    else_suite(else_suite), finally_suite(finally_suite) { }

string TryStatement::str() const {
  return "try:";
}

void TryStatement::print(FILE* stream, size_t indent_level) const {
  CompoundStatement::print(stream, indent_level);
  for (const auto& it : this->excepts) {
    if (!it) {
      fprintf(stream, "# NULL STATEMENT\n");
    } else {
      it->print(stream, indent_level);
    }
  }
  if (this->else_suite) {
    this->else_suite->print(stream, indent_level);
  }
  if (finally_suite) {
    this->finally_suite->print(stream, indent_level);
  }
}

void TryStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



WithStatement::WithStatement(
    vector<pair<shared_ptr<Expression>, string>>&& item_to_name,
    vector<shared_ptr<Statement>>&& items, size_t file_offset) :
    CompoundStatement(move(items), file_offset),
    item_to_name(move(item_to_name)) { }

string WithStatement::str() const {

  string ret = "with ";
  for (const auto& it : this->item_to_name) {
    if (ret.size() > 5) {
      ret += ", ";
    }
    ret += it.first->str();
    if (!it.second.empty()) {
      ret += " as ";
      ret += it.second;
    }
  }
  return ret + ":";
}

void WithStatement::accept(ASTVisitor* v) {
  v->visit(this);
}



FunctionDefinition::FunctionDefinition(
    vector<shared_ptr<Expression>>&& decorators, const string& name,
    FunctionArguments&& args, shared_ptr<TypeAnnotation> return_type_annotation,
    vector<shared_ptr<Statement>>&& items, size_t file_offset) :
    CompoundStatement(move(items), file_offset), decorators(move(decorators)),
    name(name), args(move(args)),
    return_type_annotation(return_type_annotation) { }

string FunctionDefinition::str() const {
  // TODO: add return type annotation here
  string prefix;
  for (const auto& decorator : this->decorators) {
    prefix += "@" + decorator->str() + "\n";
  }
  string args_str = this->args.str();
  return prefix + string_printf("def %s(%s) /*id=%" PRIu64 "*/:",
      this->name.c_str(), args_str.c_str(), this->function_id);
}

void FunctionDefinition::accept(ASTVisitor* v) {
  v->visit(this);
}



ClassDefinition::ClassDefinition(vector<shared_ptr<Expression>>&& decorators,
    const string& name, vector<shared_ptr<Expression>>&& parent_types,
    vector<shared_ptr<Statement>>&& items, size_t file_offset) :
    CompoundStatement(move(items), file_offset), decorators(move(decorators)),
    name(name), parent_types(move(parent_types)) { }

string ClassDefinition::str() const {
  if (this->parent_types.size() == 0) {
    return string_printf("class %s /*id=%" PRIu64 "*/:", this->name.c_str(),
        this->class_id);
  }
  string base_str = comma_str_list(this->parent_types);
  return string_printf("class %s(%s) /*id=%" PRIu64 "*/:", this->name.c_str(),
      base_str.c_str(), this->class_id);
}

void ClassDefinition::print(FILE* stream, size_t indent_level) const {
  for (size_t x = 0; x < this->decorators.size(); x++) {
    print_indent(stream, indent_level);
    if (!this->decorators[x]) {
      fprintf(stream, "# NULL DECORATOR\n");
    } else {
      fprintf(stream, "@%s\n", this->decorators[x]->str().c_str());
    }
  }
  CompoundStatement::print(stream, indent_level);
}

void ClassDefinition::accept(ASTVisitor* v) {
  v->visit(this);
}
