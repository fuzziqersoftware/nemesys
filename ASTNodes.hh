#pragma once

#include <memory>
#include <string>
#include <vector>


struct ASTVisitor; // forward declaration since the visitor type depends on types declared in this file


struct UnpackingFormat {
  virtual std::string str() const = 0;
  virtual void accept(ASTVisitor* v) = 0;
};

struct UnpackingTuple : UnpackingFormat {
  std::vector<std::shared_ptr<UnpackingFormat>> objects;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct UnpackingVariable : UnpackingFormat {
  std::string name;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};


struct Expression {
  virtual bool valid_lvalue() const;

  virtual std::string str() const = 0;
  virtual void accept(ASTVisitor* v) = 0;
};

enum ArgumentMode {
  DefaultArgMode = 0,
  ArgListMode, // *args
  KeywordArgListMode, // **kwargs
};

struct ArgumentDefinition {
  std::string name;
  std::shared_ptr<Expression> default_value;
  ArgumentMode mode;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

enum UnaryOperator {
  // logical operators
  LogicalNotOperator = 0,

  // bitwise operators
  NotOperator,

  // arithmetic operators
  PositiveOperator, // basically a no-op
  NegativeOperator,

  // special operators
  RepresentationOperator, // `obj` == repr(obj); not supported
  YieldOperator,

  InvalidUnaryOperation,
};

struct UnaryOperation : Expression {
  UnaryOperator oper;
  std::shared_ptr<Expression> expr;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

enum BinaryOperator {
  // logical operators
  LogicalOrOperator = 0,
  LogicalAndOperator,

  // comparison operators
  LessThanOperator,
  GreaterThanOperator,
  EqualityOperator,
  GreaterOrEqualOperator,
  LessOrEqualOperator,
  NotEqualOperator, // <> and != are both valid here
  InOperator,
  NotInOperator,
  IsOperator,
  IsNotOperator,

  // bitwise operators
  OrOperator,
  AndOperator,
  XorOperator,
  LeftShiftOperator,
  RightShiftOperator,

  // arithmetic operators
  AdditionOperator,
  SubtractionOperator,
  MultiplicationOperator,
  DivisionOperator,
  ModulusOperator,
  IntegerDivisionOperator,
  ExponentiationOperator,

  InvalidBinaryOperation,
};

struct BinaryOperation : Expression {
  BinaryOperator oper;
  std::shared_ptr<Expression> left;
  std::shared_ptr<Expression> right;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

enum TernaryOperator {
  // logical operators
  IfElseOperator = 0,

  InvalidTernaryOperation,
};

struct TernaryOperation : Expression {
  TernaryOperator oper;
  std::shared_ptr<Expression> left;
  std::shared_ptr<Expression> center;
  std::shared_ptr<Expression> right;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ListConstructor : Expression {
  std::vector<std::shared_ptr<Expression>> items;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DictConstructor : Expression {
  std::vector<pair<std::shared_ptr<Expression>, std::shared_ptr<Expression>>> items;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct SetConstructor : Expression {
  std::vector<std::shared_ptr<Expression>> items;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TupleConstructor : Expression {
  std::vector<std::shared_ptr<Expression>> items;

  virtual bool valid_lvalue() const;
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ListComprehension : Expression {
  // [item_pattern for variables in source_data if predicate]
  std::shared_ptr<Expression> item_pattern;
  std::shared_ptr<UnpackingFormat> variables;
  std::shared_ptr<Expression> source_data;
  std::shared_ptr<Expression> predicate; // can be NULL

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DictComprehension : Expression {
  // {key_pattern: value_pattern for variables in source_data if predicate}
  std::shared_ptr<Expression> key_pattern;
  std::shared_ptr<Expression> value_pattern;
  std::shared_ptr<UnpackingFormat> variables;
  std::shared_ptr<Expression> source_data;
  std::shared_ptr<Expression> predicate; // can be NULL

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct SetComprehension : Expression {
  // {item_pattern for variables in source_data if predicatre}
  std::shared_ptr<Expression> item_pattern;
  std::shared_ptr<UnpackingFormat> variables;
  std::shared_ptr<Expression> source_data;
  std::shared_ptr<Expression> predicate; // can be NULL

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct LambdaDefinition : Expression {
  std::vector<std::shared_ptr<ArgumentDefinition>> args;
  std::shared_ptr<Expression> result;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FunctionCall : Expression {
  std::shared_ptr<Expression> function;
  std::vector<std::shared_ptr<ArgumentDefinition>> args;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ArrayIndex : Expression {
  std::shared_ptr<Expression> array;
  std::shared_ptr<Expression> index;

  virtual bool valid_lvalue() const;
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ArraySlice : Expression {
  std::shared_ptr<Expression> array;
  std::shared_ptr<Expression> slice_left;
  std::shared_ptr<Expression> slice_right;
  // TODO: step argument

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct IntegerConstant : Expression {
  long value;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FloatConstant : Expression {
  double value;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct StringConstant : Expression {
  std::string value;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TrueConstant : Expression {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FalseConstant : Expression {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct NoneConstant : Expression {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct VariableLookup : Expression {
  std::string name;

  virtual bool valid_lvalue() const;
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct AttributeLookup : Expression {
  std::shared_ptr<Expression> left;
  std::string right;

  virtual bool valid_lvalue() const;
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};


struct Statement {
  virtual void print(int indent_level = 0) const = 0;
  virtual std::string str() const = 0;
  virtual void accept(ASTVisitor* v) = 0;
};

struct SimpleStatement : Statement {
  virtual void print(int indent_level = 0) const;
  virtual void accept(ASTVisitor* v) = 0;
};

struct CompoundStatement : Statement {
  std::vector<std::shared_ptr<Statement>> items;

  virtual void print(int indent_level = 0) const;
  virtual void accept(ASTVisitor* v) = 0;
};

struct ModuleStatement : CompoundStatement {
  virtual void print(int indent_level) const;
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ExpressionStatement : SimpleStatement {
  std::shared_ptr<Expression> expr;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct AssignmentStatement : SimpleStatement {
  std::vector<std::shared_ptr<Expression>> left;
  std::vector<std::shared_ptr<Expression>> right;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

enum AugmentOperator {
  PlusEqualsOperator = 0,
  MinusEqualsOperator,
  AsteriskEqualsOperator,
  SlashEqualsOperator,
  PercentEqualsOperator,
  AndEqualsOperator,
  OrEqualsOperator,
  XorEqualsOperator,
  LeftShiftEqualsOperator,
  RightShiftEqualsOperator,
  DoubleTimesEqualsOperator,
  DoubleSlashEqualsOperator,
  _AugmentOperatorCount,
};

struct AugmentStatement : SimpleStatement {
  AugmentOperator oper;
  std::vector<std::shared_ptr<Expression>> left;
  std::vector<std::shared_ptr<Expression>> right;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct PrintStatement : SimpleStatement {
  std::shared_ptr<Expression> stream;
  std::vector<std::shared_ptr<Expression>> items;
  bool suppress_newline;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DeleteStatement : SimpleStatement {
  std::vector<std::shared_ptr<Expression>> items;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct PassStatement : SimpleStatement {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FlowStatement : SimpleStatement { };

struct ImportStatement : SimpleStatement {
  // 1. import name1 [as rename1], module_name2 [as rename2]
  // 2. from name1 import symbol1 [as symbol_rename1], symbol2 [as symbol_rename2]
  // 3. from name1 import *

  std::vector<std::string> names; // length 1 if cases 2 or 3
  std::vector<std::string> renames; // length 1 if cases 2 or 3
  std::vector<std::string> symbols; // nonempty if case 2
  std::vector<std::string> symbol_renames; // possibly nonempty if case 2
  bool import_star; // true iff case 3

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct GlobalStatement : SimpleStatement {
  std::vector<std::string> names;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ExecStatement : SimpleStatement {
  std::shared_ptr<Expression> code;
  std::shared_ptr<Expression> globals;
  std::shared_ptr<Expression> locals;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct AssertStatement : SimpleStatement {
  std::shared_ptr<Expression> check;
  std::shared_ptr<Expression> failure_message;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct BreakStatement : FlowStatement {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ContinueStatement : FlowStatement {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ReturnStatement : FlowStatement {
  std::vector<std::shared_ptr<Expression> > items;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct RaiseStatement : FlowStatement {
  std::shared_ptr<Expression> type;
  std::shared_ptr<Expression> value;
  std::shared_ptr<Expression> traceback;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct YieldStatement : FlowStatement {
  std::shared_ptr<Expression> expr; // if NULL, yields None

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct SingleIfStatement : CompoundStatement {
  std::shared_ptr<Expression> check;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ElseStatement : CompoundStatement {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ElifStatement : SingleIfStatement {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct IfStatement : SingleIfStatement {
  std::vector<std::shared_ptr<ElifStatement>> elifs;
  std::shared_ptr<ElseStatement> else_suite; // may be NULL

  virtual std::string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct ForStatement : CompoundStatement {
  std::shared_ptr<UnpackingFormat> variables;
  std::vector<std::shared_ptr<Expression>> in_exprs;
  std::shared_ptr<ElseStatement> else_suite; // may be NULL

  virtual std::string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct WhileStatement : CompoundStatement {
  std::shared_ptr<Expression> condition;
  std::shared_ptr<ElseStatement> else_suite; // may be NULL

  virtual std::string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct ExceptStatement : CompoundStatement {
  std::shared_ptr<Expression> types; // can be NULL for default except clause
  std::string name;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FinallyStatement : CompoundStatement {
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TryStatement : CompoundStatement {
  std::vector<std::shared_ptr<ExceptStatement>> excepts;
  std::shared_ptr<ElseStatement> else_suite; // may be NULL
  std::shared_ptr<FinallyStatement> finally_suite; // may be NULL

  virtual std::string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct WithStatement : CompoundStatement {
  std::vector<std::shared_ptr<Expression>> items;
  std::vector<std::string> names;

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FunctionDefinition : CompoundStatement {
  std::string name;
  std::vector<std::shared_ptr<ArgumentDefinition>> args;
  std::vector<std::shared_ptr<Expression>> decorators;

  virtual std::string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct ClassDefinition : CompoundStatement {
  std::string name;
  std::vector<std::shared_ptr<Expression>> parent_types;
  std::vector<std::shared_ptr<Expression>> decorators;

  virtual std::string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};
