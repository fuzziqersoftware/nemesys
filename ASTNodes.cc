#include <stdio.h>

#include <memory>
#include <string>
#include <vector>

#include "ast.hh"
#include "ast_visitor.hh"

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
static string str_or_null(const shared_ptr<T> item) {
  return item.get() ? item->str() : "NULL";
}

static void print_indent(FILE* stream, int level) {
  while (level--) {
    printf(stream, " "); // TODO: this should be putc or soemthing
  }
}

const char* augment_operator_names[] = {
  "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=", "**=", "//=", NULL};



// UnpackingTuple

string UnpackingTuple::str() const {
  return "(" + comma_str_list(this->objects) + ")";
}

void UnpackingTuple::accept(ASTVisitor* v) {
  v->visit(this);
}

// UnpackingVariable

string UnpackingVariable::str() const {
  return this->name;
}

void UnpackingVariable::accept(ASTVisitor* v) {
  v->visit(this);
}

// Expression

bool Expression::valid_lvalue() const {
  return false;
}

// ArgumentDefinition

string ArgumentDefinition::str() const {
  if (this->mode == DefaultArgMode) {
    if (!this->name.empty()) {
      return this->name + (!this->default_value ? "" :
          ("=" + this->default_value->str()));
    } else {
      return str_or_null(this->default_value);
    }
  }
  if (this->mode == ArgListMode) {
    if (!this->default_value) {
      return "*" + this->name;
    } else {
      return "*" + str_or_null(this->default_value);
    }
  }
  if (this->mode == KeywordArgListMode) {
    if (!this->default_value) {
      return "**" + this->name;
    } else {
      return "**" + str_or_null(this->default_value);
    }
  }
  return "(BAD ARGUMENT DEFINITION)";
}

void ArgumentDefinition::accept(ASTVisitor* v) {
  v->visit(this);
}

// UnaryOperation

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
  if (this->oper == RepresentationOperator) {
    return string_printf("repr(%s)", expr_str.c_str());
  }
  return string_printf("(%s%s)", unary_operator_names[this->oper],
      expr_str.c_str());
}

void UnaryOperation::accept(ASTVisitor* v) {
  v->visit(this);
}

// BinaryOperation

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
      binary_operator_names[this->oper], right_str.c_str());
}

void BinaryOperation::accept(ASTVisitor* v) {
  v->visit(this);
}

// TernaryOperation

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

// ListConstructor

string ListConstructor::str() const {
  return "[" + comma_str_list(this->items) + "]";
}

void ListConstructor::accept(ASTVisitor* v) {
  v->visit(this);
}

// DictConstructor

string DictConstructor::str() const {
  string ret;
  for (const auto& it : this->items) {
    if (!ret.empty()) {
      ret += ", ";
    }
    ret += it.first.str();
    ret += ": ";
    ret += it.second.str();
  }
  return "{" + ret + "}";
}

void DictConstructor::accept(ASTVisitor* v) {
  v->visit(this);
}

// SetConstructor

string SetConstructor::str() const {
  return "set(" + comma_str_list(this->items) + ")";
}

void SetConstructor::accept(ASTVisitor* v) {
  v->visit(this);
}

// TupleConstructor

string TupleConstructor::str() const {
  return "(" + comma_str_list(this->items) + ")";
}

bool TupleConstructor::valid_lvalue() const {
  // a TupleConstructor is a valid lvalue if all of its items are valid lvalues
  // and it has at least one item
  for (const auto& it : this->items) {
    if (!it->valid_lvalue()) {
      return false;
    }
  }
  return (!this->items.empty());
}

void TupleConstructor::accept(ASTVisitor* v) {
  v->visit(this);
}

// ListComprehension

string ListComprehension::str() const {
  string item_str = this->item_pattern->str();
  string vars_str = this->variables->str();
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

// DictComprehension

string DictComprehension::str() const {
  string key_str = this->key_pattern->str();
  string value_str = this->value_pattern->str();
  string vars_str = this->variables->str();
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

// SetComprehension

string SetComprehension::str() const {
  string item_str = this->item_pattern->str();
  string vars_str = this->variables->str();
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

// LambdaDefinition

string LambdaDefinition::str() const {
  return "lambda " + comma_str_list(this->args) + ": " + this->result->str();
}

void LambdaDefinition::accept(ASTVisitor* v) {
  v->visit(this);
}

// FunctionCall

string FunctionCall::str() const {
  return this->function->str() + "(" + comma_str_list(this->args) + ")";
}

void FunctionCall::accept(ASTVisitor* v) {
  v->visit(this);
}

// ArrayIndex

string ArrayIndex::str() const {
  return this->array->str() + "[" + this->index->str() + "]";
}

bool ArrayIndex::valid_lvalue() const {
  return true;
}

void ArrayIndex::accept(ASTVisitor* v) {
  v->visit(this);
}

// ArraySlice

string ArraySlice::str() const {
  return this->array->str() + "[" + str_or_null(this->slice_left) + ":" +
      str_or_null(this->slice_right) + "]";
}

void ArraySlice::accept(ASTVisitor* v) {
  v->visit(this);
}

// IntegerConstant

string IntegerConstant::str() const {
  return to_string(this->value);
}

void IntegerConstant::accept(ASTVisitor* v) {
  v->visit(this);
}

// FloatingConstant

string FloatingConstant::str() const {
  return to_string(this->value);
}

void FloatingConstant::accept(ASTVisitor* v) {
  v->visit(this);
}

// StringConstant

string StringConstant::str() const {
  // TODO: we should escape quotes in the string, lol
  return "\'" + this->value + "\'";
}

void StringConstant::accept(ASTVisitor* v) {
  v->visit(this);
}

// TrueConstant

string TrueConstant::str() const {
  return "True";
}

void TrueConstant::accept(ASTVisitor* v) {
  v->visit(this);
}

// FalseConstant

string FalseConstant::str() const {
  return "False";
}

void FalseConstant::accept(ASTVisitor* v) {
  v->visit(this);
}

// NoneConstant

string NoneConstant::str() const {
  return "None";
}

void NoneConstant::accept(ASTVisitor* v) {
  v->visit(this);
}

// VariableLookup

string VariableLookup::str() const {
  return this->name;
}

bool VariableLookup::valid_lvalue() const {
  return true;
}

void VariableLookup::accept(ASTVisitor* v) {
  v->visit(this);
}

// AttributeLookup

string AttributeLookup::str() const {
  return this->left->str() + "." + this->right;
}

bool AttributeLookup::valid_lvalue() const {
  return true;
}

void AttributeLookup::accept(ASTVisitor* v) {
  v->visit(this);
}



// statement functions



// Statement

// SimpleStatement

void SimpleStatement::print(FILE* stream, int indent_level) const {
  print_indent(stream, indent_level);
  string s = this->str();
  fprintf(stream, "%s\n", s.c_str());
}

// CompoundStatement

void CompoundStatement::print(FILE* stream, int indent_level) const {
  print_indent(stream, indent_level);
  string s = this->str();
  printf(stream, "%s\n", s.c_str());
  for (const auto& it : this->items) {
    if (!it) {
      printf(stream, "# NULL STATEMENT\n");
    } else {
      it->print(stream, indent_level + 2);
    }
  }
}

// ModuleStatement

string ModuleStatement::str() const {
  return "# ModuleStatement";
}

void ModuleStatement::print(FILE* stream, int indent_level) const {
  // basically the same as the base class method except it doesn't indent
  print_indent(stream, indent_level);
  string s = this->str();
  printf(stream, "%s\n", s.c_str());
  for (const auto& it : this->items) {
    if (!it) {
      printf(stream, "# NULL STATEMENT\n");
    } else {
      it->print(stream, indent_level);
    }
  }
}

void ModuleStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// ExpressionStatement

string ExpressionStatement::str() const {
  return this->expr.str();
}

void ExpressionStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// AssignmentStatement

string AssignmentStatement::str() const {
  return comma_str_list(this->left) + " = " + comma_str_list(this->right);
}

void AssignmentStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// AugmentStatement

string AugmentStatement::str() const {
  return comma_str_list(this->left) + " " + augment_operator_names[oper] + " " +
      comma_str_list(this->right);
}

void AugmentStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// PrintStatement

string PrintStatement::str() const {
  if (!this->stream) {
    return "print " + comma_str_list(this->items) +
        (this->suppress_newline ? "," : "");
  }
  return "print >> " + this->stream->str() + ", " +
      comma_str_list(this->items) + (this->suppress_newline ? "," : "");
}

void PrintStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// DeleteStatement

string DeleteStatement::str() const {
  return "del " + comma_str_list(this->items);
}

void DeleteStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// PassStatement

string PassStatement::str() const {
  return "pass";
}

void PassStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// FlowStatement

// ImportStatement

string ImportStatement::str() const {
  if (this->import_star) {
    return "from " + comma_list(this->module_names) + " import *";
  }
  if (!this->module_renames.empty()) {
    // TODO: this is wrong; it produces "x, y as a, b" instead of "x as a, y as b"
    return "import " + comma_list(this->module_names) + " as " +
        comma_list(this->module_renames);
  }
  if (!this->symbol_renames.empty()) {
    // TODO: this is wrong for the same reason as above
    return "from " + comma_list(this->module_names) + " import " + comma_list(this->symbol_list) + " as " + comma_list(symbol_renames);
  }
  if (!this->symbol_list.empty()) {
    return "from " + comma_list(this->module_names) + " import " + comma_list(this->symbol_list);
  }
  return "import " + comma_list(this->module_names);
}

void ImportStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// GlobalStatement

string GlobalStatement::str() const {
  return "global " + comma_list(this->names);
}

void GlobalStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// ExecStatement

string ExecStatement::str() const {
  return "exec " + this->code.str() + ", " + str_or_null(this->globals) +
      ", " + str_or_null(this->locals);
}

void ExecStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// AssertStatement

string AssertStatement::str() const {
  return "assert " + this->check->str() + ", " +
      str_or_null(this->failure_message);
}

void AssertStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// BreakStatement

string BreakStatement::str() const {
  return "break";
}

void BreakStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// ContinueStatement

string ContinueStatement::str() const {
  return "continue";
}

void ContinueStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// ReturnStatement

string ReturnStatement::str() const {
  return "return " + comma_str_list(this->items);
}

void ReturnStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// RaiseStatement

string RaiseStatement::str() const {
  return "raise " + this->type->str() + ", " + str_or_null(this->value) +
      ", " + str_or_null(this->traceback);
}

void RaiseStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// YieldStatement

string YieldStatement::str() const {
  return "yield " + str_or_null(this->expr);
}

void YieldStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// SingleIfStatement

string SingleIfStatement::str() const {
  return "if " + this->check->str() + ":";
}

void SingleIfStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// ElseStatement

string ElseStatement::str() const {
  return "else:";
};

void ElseStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// IfStatement

string IfStatement::str() const {
  return "if " + this->check->str() + ":";
}

void IfStatement::print(FILE* stream, int indent_level) const {
  CompoundStatement::print(stream, indent_level);
  for (const auto& it : this->elifs) {
    if (!it) {
      printf(stream, "# NULL STATEMENT\n");
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

// ElifStatement

string ElifStatement::str() const {
  return "elif " + this->check->str() + ":";
}

void ElifStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// ForStatement

string ForStatement::str() const {
  return "for " + this->variables->str() + " in " +
      comma_str_list(this->in_exprs) + ":";
}

void ForStatement::print(FILE* stream, int indent_level) const {
  CompoundStatement::print(stream, indent_level);
  if (this->else_suite) {
    this->else_suite->print(stream, indent_level);
  }
}

void ForStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// WhileStatement

string WhileStatement::str() const {
  return "while " + this->condition->str() + ":";
}

void WhileStatement::print(FILE* stream, int indent_level) const {
  CompoundStatement::print(stream, indent_level);
  if (this->else_suite) {
    this->else_suite->print(stream, indent_level);
  }
}

void WhileStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// ExceptStatement

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

// FinallyStatement

string FinallyStatement::str() const {
  return "finally:";
};

void FinallyStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// TryStatement

string TryStatement::str() const {
  return "try:";
}

void TryStatement::print(FILE* stream, int indent_level) const {
  CompoundStatement::print(stream, indent_level);
  for (const auto& it : this->excepts) {
    if (!it) {
      printf(stream, "# NULL STATEMENT\n");
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

// WithStatement

string WithStatement::str() const {

  string ret = "with ";
  for (size_t x = 0; x < items.size(); x++) {
    if (x > 0) {
      ret += ", ";
    }
    ret += this->items[x]->str();
    if (!this->names.empty() && !this->names[x].empty()) {
      ret += " as ";
      ret += this->names[x];
    }
  }
  return ret + ":";
}

void WithStatement::accept(ASTVisitor* v) {
  v->visit(this);
}

// FunctionDefinition

string FunctionDefinition::str() const {
  return "def " + this->name + "(" + comma_str_list(this->args) + "):";
}

void FunctionDefinition::print(FILE* stream, int indent_level) const {
  for (size_t x = 0; x < this->decorators.size(); x++) {
    print_indent(stream, indent_level);
    if (!this->decorators[x]) {
      printf(stream, "# NULL DECORATOR\n");
    } else {
      printf(stream, "@%s\n", this->decorators[x]->str().c_str());
    }
  }
  CompoundStatement::print(stream, indent_level);
}

void FunctionDefinition::accept(ASTVisitor* v) {
  v->visit(this);
}

// ClassDefinition

string ClassDefinition::str() const {
  if (this->parent_types.size() == 0) {
    return "class " + this->name + ":";
  }
  return "class " + this->name + "(" + comma_str_list(this->parent_types) +
      "):";
}

void ClassDefinition::print(FILE* stream, int indent_level) const {
  for (size_t x = 0; x < this->decorators.size(); x++) {
    print_indent(stream, indent_level);
    if (!this->decorators[x]) {
      printf(stream, "# NULL DECORATOR\n");
    } else {
      printf(stream, "@%s\n", this->decorators[x]->str().c_str());
    }
  }
  CompoundStatement::print(stream, indent_level);
}

void ClassDefinition::accept(ASTVisitor* v) {
  v->visit(this);
}
