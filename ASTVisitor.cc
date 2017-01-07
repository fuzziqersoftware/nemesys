#include <stdio.h>

#include <memory>
#include <string>
#include <vector>

#include "ast.hh"
#include "ast_visitor.hh"

using namespace std;



template <typename T> void ASTVisitor::visit_list(vector<shared_ptr<T> >& list) {
  for (int x = 0; x < list.size(); x++)
    list[x]->accept(this);
}



void ASTVisitor::visit(UnpackingTuple* a) { }
void ASTVisitor::visit(UnpackingVariable* a) { }
void ASTVisitor::visit(ArgumentDefinition* a) { }
void ASTVisitor::visit(UnaryOperation* a) { }
void ASTVisitor::visit(BinaryOperation* a) { }
void ASTVisitor::visit(TernaryOperation* a) { }
void ASTVisitor::visit(ListConstructor* a) { }
void ASTVisitor::visit(DictConstructor* a) { }
void ASTVisitor::visit(SetConstructor* a) { }
void ASTVisitor::visit(TupleConstructor* a) { }
void ASTVisitor::visit(ListComprehension* a) { }
void ASTVisitor::visit(DictComprehension* a) { }
void ASTVisitor::visit(SetComprehension* a) { }
void ASTVisitor::visit(LambdaDefinition* a) { }
void ASTVisitor::visit(FunctionCall* a) { }
void ASTVisitor::visit(ArrayIndex* a) { }
void ASTVisitor::visit(ArraySlice* a) { }
void ASTVisitor::visit(IntegerConstant* a) { }
void ASTVisitor::visit(FloatingConstant* a) { }
void ASTVisitor::visit(StringConstant* a) { }
void ASTVisitor::visit(TrueConstant* a) { }
void ASTVisitor::visit(FalseConstant* a) { }
void ASTVisitor::visit(NoneConstant* a) { }
void ASTVisitor::visit(VariableLookup* a) { }
void ASTVisitor::visit(AttributeLookup* a) { }
void ASTVisitor::visit(ModuleStatement* a) { }
void ASTVisitor::visit(ExpressionStatement* a) { }
void ASTVisitor::visit(AssignmentStatement* a) { }
void ASTVisitor::visit(AugmentStatement* a) { }
void ASTVisitor::visit(PrintStatement* a) { }
void ASTVisitor::visit(DeleteStatement* a) { }
void ASTVisitor::visit(PassStatement* a) { }
void ASTVisitor::visit(ImportStatement* a) { }
void ASTVisitor::visit(GlobalStatement* a) { }
void ASTVisitor::visit(ExecStatement* a) { }
void ASTVisitor::visit(AssertStatement* a) { }
void ASTVisitor::visit(BreakStatement* a) { }
void ASTVisitor::visit(ContinueStatement* a) { }
void ASTVisitor::visit(ReturnStatement* a) { }
void ASTVisitor::visit(RaiseStatement* a) { }
void ASTVisitor::visit(YieldStatement* a) { }
void ASTVisitor::visit(SingleIfStatement* a) { }
void ASTVisitor::visit(ElseStatement* a) { }
void ASTVisitor::visit(IfStatement* a) { }
void ASTVisitor::visit(ElifStatement* a) { }
void ASTVisitor::visit(ForStatement* a) { }
void ASTVisitor::visit(WhileStatement* a) { }
void ASTVisitor::visit(ExceptStatement* a) { }
void ASTVisitor::visit(FinallyStatement* a) { }
void ASTVisitor::visit(TryStatement* a) { }
void ASTVisitor::visit(WithStatement* a) { }
void ASTVisitor::visit(FunctionDefinition* a) { }
void ASTVisitor::visit(ClassDefinition* a) { }



void RecursiveASTVisitor::visit(UnpackingTuple* a) {
  visit_list(a->objects);
}

void RecursiveASTVisitor::visit(UnpackingVariable* a) { }

void RecursiveASTVisitor::visit(ArgumentDefinition* a) {
  if (a->default_value)
    a->default_value->accept(this);
}

void RecursiveASTVisitor::visit(UnaryOperation* a) {
  a->expr->accept(this);
}

void RecursiveASTVisitor::visit(BinaryOperation* a) {
  a->left->accept(this);
  a->right->accept(this);
}

void RecursiveASTVisitor::visit(TernaryOperation* a) {
  a->left->accept(this);
  a->center->accept(this);
  a->right->accept(this);
}

void RecursiveASTVisitor::visit(ListConstructor* a) {
  visit_list(a->items);
}

void RecursiveASTVisitor::visit(DictConstructor* a) {
  for (size_t x = 0; x < a->items.size(); x++) {
    a->items[x].first->accept(this);
    a->items[x].second->accept(this);
  }
}

void RecursiveASTVisitor::visit(SetConstructor* a) {
  visit_list(a->items);
}

void RecursiveASTVisitor::visit(TupleConstructor* a) {
  visit_list(a->items);
}

void RecursiveASTVisitor::visit(ListComprehension* a) {
  a->item_pattern->accept(this);
  a->variables->accept(this);
  a->source_data->accept(this);
  if (a->if_expr)
    a->if_expr->accept(this);
}

void RecursiveASTVisitor::visit(DictComprehension* a) {
  a->key_pattern->accept(this);
  a->value_pattern->accept(this);
  a->variables->accept(this);
  a->source_data->accept(this);
  if (a->if_expr)
    a->if_expr->accept(this);
}

void RecursiveASTVisitor::visit(SetComprehension* a) {
  a->item_pattern->accept(this);
  a->variables->accept(this);
  a->source_data->accept(this);
  if (a->if_expr)
    a->if_expr->accept(this);
}

void RecursiveASTVisitor::visit(LambdaDefinition* a) {
  visit_list(a->args);
  a->result->accept(this);
}

void RecursiveASTVisitor::visit(FunctionCall* a) {
  a->function->accept(this);
  visit_list(a->args);
}

void RecursiveASTVisitor::visit(ArrayIndex* a) {
  a->array->accept(this);
  a->index->accept(this);
}

void RecursiveASTVisitor::visit(ArraySlice* a) {
  a->array->accept(this);
  if (a->slice_left)
    a->slice_left->accept(this);
  if (a->slice_right)
    a->slice_right->accept(this);
}

void RecursiveASTVisitor::visit(IntegerConstant* a) { }
void RecursiveASTVisitor::visit(FloatingConstant* a) { }
void RecursiveASTVisitor::visit(StringConstant* a) { }
void RecursiveASTVisitor::visit(TrueConstant* a) { }
void RecursiveASTVisitor::visit(FalseConstant* a) { }
void RecursiveASTVisitor::visit(NoneConstant* a) { }
void RecursiveASTVisitor::visit(VariableLookup* a) { }

void RecursiveASTVisitor::visit(AttributeLookup* a) {
  a->left->accept(this);
}



void RecursiveASTVisitor::visit(ModuleStatement* a) {
  visit_list(a->suite);
}

void RecursiveASTVisitor::visit(ExpressionStatement* a) {
  a->expr->accept(this);
}

void RecursiveASTVisitor::visit(AssignmentStatement* a) {
  visit_list(a->left);
  visit_list(a->right);
}

void RecursiveASTVisitor::visit(AugmentStatement* a) {
  visit_list(a->left);
  visit_list(a->right);
}

void RecursiveASTVisitor::visit(PrintStatement* a) {
  if (a->stream)
    a->stream->accept(this);
  visit_list(a->items);
}

void RecursiveASTVisitor::visit(DeleteStatement* a) {
  visit_list(a->items);
}

void RecursiveASTVisitor::visit(PassStatement* a) { }
void RecursiveASTVisitor::visit(ImportStatement* a) { }
void RecursiveASTVisitor::visit(GlobalStatement* a) { }

void RecursiveASTVisitor::visit(ExecStatement* a) {
  a->code->accept(this);
  if (a->globals)
    a->globals->accept(this);
  if (a->locals)
    a->locals->accept(this);
}

void RecursiveASTVisitor::visit(AssertStatement* a) {
  a->check->accept(this);
  if (a->failure_message)
    a->failure_message->accept(this);
}

void RecursiveASTVisitor::visit(BreakStatement* a) { }
void RecursiveASTVisitor::visit(ContinueStatement* a) { }

void RecursiveASTVisitor::visit(ReturnStatement* a) {
  visit_list(a->items);
}

void RecursiveASTVisitor::visit(RaiseStatement* a) {
  if (a->type)
    a->type->accept(this);
  if (a->value)
    a->value->accept(this);
  if (a->traceback)
    a->traceback->accept(this);
}

void RecursiveASTVisitor::visit(YieldStatement* a) {
  if (a->expr)
    a->expr->accept(this);
}

void RecursiveASTVisitor::visit(SingleIfStatement* a) {
  a->check->accept(this);
  visit_list(a->suite);
}

void RecursiveASTVisitor::visit(ElseStatement* a) {
  visit_list(a->suite);
}

void RecursiveASTVisitor::visit(IfStatement* a) {
  a->check->accept(this);
  visit_list(a->suite);
  visit_list(a->elifs);
  if (a->else_suite)
    a->else_suite->accept(this);
}

void RecursiveASTVisitor::visit(ElifStatement* a) {
  a->check->accept(this);
  visit_list(a->suite);
}

void RecursiveASTVisitor::visit(ForStatement* a) {
  a->variables->accept(this);
  visit_list(a->in_exprs);
  visit_list(a->suite);
  if (a->else_suite)
    a->else_suite->accept(this);
}

void RecursiveASTVisitor::visit(WhileStatement* a) {
  a->condition->accept(this);
  visit_list(a->suite);
  if (a->else_suite)
    a->else_suite->accept(this);
}

void RecursiveASTVisitor::visit(ExceptStatement* a) {
  if (a->types)
    a->types->accept(this);
  visit_list(a->suite);
}

void RecursiveASTVisitor::visit(FinallyStatement* a) {
  visit_list(a->suite);
}

void RecursiveASTVisitor::visit(TryStatement* a) {
  visit_list(a->suite);
  visit_list(a->excepts);
  if (a->else_suite)
    a->else_suite->accept(this);
  if (a->finally_suite)
    a->finally_suite->accept(this);
}

void RecursiveASTVisitor::visit(WithStatement* a) {
  visit_list(a->items);
  visit_list(a->suite);
}

void RecursiveASTVisitor::visit(FunctionDefinition* a) {
  visit_list(a->decorators);
  visit_list(a->args);
  visit_list(a->suite);
}

void RecursiveASTVisitor::visit(ClassDefinition* a) {
  visit_list(a->decorators);
  visit_list(a->parent_types);
  visit_list(a->suite);
}