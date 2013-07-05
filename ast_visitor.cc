#include <tr1/memory>
#include <stdio.h>

#include <string>
#include <vector>

using namespace std;
using namespace std::tr1;

#include "ast.hh"
#include "ast_visitor.hh"



template <typename T> void ASTVisitor::visit_list(vector<shared_ptr<T> >& list) {
  for (int x = 0; x < list.size(); x++)
    list[x]->accept(this);
}



void ASTVisitor::visit(UnpackingTuple* a) {
  visit_list(a->objects);
}

void ASTVisitor::visit(UnpackingVariable* a) { }

void ASTVisitor::visit(ArgumentDefinition* a) {
  if (a->default_value)
    a->default_value->accept(this);
}

void ASTVisitor::visit(UnaryOperation* a) {
  a->expr->accept(this);
}

void ASTVisitor::visit(BinaryOperation* a) {
  a->left->accept(this);
  a->right->accept(this);
}

void ASTVisitor::visit(TernaryOperation* a) {
  a->left->accept(this);
  a->center->accept(this);
  a->right->accept(this);
}

void ASTVisitor::visit(ListConstructor* a) {
  visit_list(a->items);
}

void ASTVisitor::visit(DictConstructor* a) {
  for (int x = 0; x < a->items.size(); x++) {
    a->items[x].first->accept(this);
    a->items[x].second->accept(this);
  }
}

void ASTVisitor::visit(SetConstructor* a) {
  visit_list(a->items);
}

void ASTVisitor::visit(TupleConstructor* a) {
  visit_list(a->items);
}

void ASTVisitor::visit(ListComprehension* a) {
  a->item_pattern->accept(this);
  a->variables->accept(this);
  a->source_data->accept(this);
  if (a->if_expr)
    a->if_expr->accept(this);
}

void ASTVisitor::visit(DictComprehension* a) {
  a->key_pattern->accept(this);
  a->value_pattern->accept(this);
  a->variables->accept(this);
  a->source_data->accept(this);
  if (a->if_expr)
    a->if_expr->accept(this);
}

void ASTVisitor::visit(SetComprehension* a) {
  a->item_pattern->accept(this);
  a->variables->accept(this);
  a->source_data->accept(this);
  if (a->if_expr)
    a->if_expr->accept(this);
}

void ASTVisitor::visit(LambdaDefinition* a) {
  visit_list(a->args);
  a->result->accept(this);
}

void ASTVisitor::visit(FunctionCall* a) {
  a->function->accept(this);
  visit_list(a->args);
}

void ASTVisitor::visit(ArrayIndex* a) {
  a->array->accept(this);
  a->index->accept(this);
}

void ASTVisitor::visit(ArraySlice* a) {
  a->array->accept(this);
  if (a->slice_left)
    a->slice_left->accept(this);
  if (a->slice_right)
    a->slice_right->accept(this);
}

void ASTVisitor::visit(IntegerConstant* a) { }
void ASTVisitor::visit(FloatingConstant* a) { }
void ASTVisitor::visit(StringConstant* a) { }
void ASTVisitor::visit(TrueConstant* a) { }
void ASTVisitor::visit(FalseConstant* a) { }
void ASTVisitor::visit(NoneConstant* a) { }
void ASTVisitor::visit(VariableLookup* a) { }

void ASTVisitor::visit(AttributeLookup* a) {
  a->left->accept(this);
  a->right->accept(this);
}



void ASTVisitor::visit(ModuleStatement* a) {
  visit_list(a->suite);
}

void ASTVisitor::visit(ExpressionStatement* a) {
  a->expr->accept(this);
}

void ASTVisitor::visit(AssignmentStatement* a) {
  visit_list(a->left);
  visit_list(a->right);
}

void ASTVisitor::visit(AugmentStatement* a) {
  visit_list(a->left);
  visit_list(a->right);
}

void ASTVisitor::visit(PrintStatement* a) {
  if (a->stream)
    a->stream->accept(this);
  visit_list(a->items);
}

void ASTVisitor::visit(DeleteStatement* a) {
  visit_list(a->items);
}

void ASTVisitor::visit(PassStatement* a) { }
void ASTVisitor::visit(ImportStatement* a) { }
void ASTVisitor::visit(GlobalStatement* a) { }

void ASTVisitor::visit(ExecStatement* a) {
  a->code->accept(this);
  if (a->globals)
    a->globals->accept(this);
  if (a->locals)
    a->locals->accept(this);
}

void ASTVisitor::visit(AssertStatement* a) {
  a->check->accept(this);
  if (a->failure_message)
    a->failure_message->accept(this);
}

void ASTVisitor::visit(BreakStatement* a) { }
void ASTVisitor::visit(ContinueStatement* a) { }

void ASTVisitor::visit(ReturnStatement* a) {
  visit_list(a->items);
}

void ASTVisitor::visit(RaiseStatement* a) {
  if (a->type)
    a->type->accept(this);
  if (a->value)
    a->value->accept(this);
  if (a->traceback)
    a->traceback->accept(this);
}

void ASTVisitor::visit(YieldStatement* a) {
  if (a->expr)
    a->expr->accept(this);
}

void ASTVisitor::visit(SingleIfStatement* a) {
  a->check->accept(this);
  visit_list(a->suite);
}

void ASTVisitor::visit(ElseStatement* a) {
  visit_list(a->suite);
}

void ASTVisitor::visit(IfStatement* a) {
  a->check->accept(this);
  visit_list(a->suite);
  visit_list(a->elifs);
  if (a->else_suite)
    a->else_suite->accept(this);
}

void ASTVisitor::visit(ElifStatement* a) {
  a->check->accept(this);
  visit_list(a->suite);
}

void ASTVisitor::visit(ForStatement* a) {
  a->variables->accept(this);
  visit_list(a->in_exprs);
  visit_list(a->suite);
  if (a->else_suite)
    a->else_suite->accept(this);
}

void ASTVisitor::visit(WhileStatement* a) {
  a->condition->accept(this);
  visit_list(a->suite);
  if (a->else_suite)
    a->else_suite->accept(this);
}

void ASTVisitor::visit(ExceptStatement* a) {
  if (a->types)
    a->types->accept(this);
  visit_list(a->suite);
}

void ASTVisitor::visit(FinallyStatement* a) {
  visit_list(a->suite);
}

void ASTVisitor::visit(TryStatement* a) {
  visit_list(a->suite);
  visit_list(a->excepts);
  if (a->else_suite)
    a->else_suite->accept(this);
  if (a->finally_suite)
    a->finally_suite->accept(this);
}

void ASTVisitor::visit(WithStatement* a) {
  visit_list(a->items);
  visit_list(a->suite);
}

void ASTVisitor::visit(FunctionDefinition* a) {
  visit_list(a->decorators);
  visit_list(a->args);
  visit_list(a->suite);
}

void ASTVisitor::visit(ClassDefinition* a) {
  visit_list(a->decorators);
  visit_list(a->parent_types);
  visit_list(a->suite);
}
