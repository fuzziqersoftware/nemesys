#ifndef _AST_HH
#define _AST_HH

#include <tr1/memory>

struct ASTVisitor; // forward declaration since the visitor type depends on types declared in this file

using namespace std;
using namespace std::tr1;

#define AST_PRINT_INDENT_STEP 2

struct UnpackingFormat {
  virtual string str() const = 0;
  virtual void accept(ASTVisitor* v) = 0;
};

struct UnpackingTuple : UnpackingFormat {
  vector<shared_ptr<UnpackingFormat> > objects;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct UnpackingVariable : UnpackingFormat {
  string name;

  virtual string str() const;
  UnpackingVariable(string);
  virtual void accept(ASTVisitor* v);
};

struct Expression {
  virtual string str() const = 0;
  virtual bool valid_lvalue() const;
  virtual void accept(ASTVisitor* v) = 0;
};

enum ArgumentMode {
  DefaultArgMode = 0,
  ArgListMode, // *args
  KeywordArgListMode, // **kwargs
};

struct ArgumentDefinition {
  string name;
  shared_ptr<Expression> default_value;
  ArgumentMode mode;

  virtual string str() const;
  ArgumentDefinition(string, shared_ptr<Expression>, ArgumentMode);
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
  shared_ptr<Expression> expr;

  virtual string str() const;
  UnaryOperation(UnaryOperator, shared_ptr<Expression>);
  UnaryOperation();
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
  shared_ptr<Expression> left;
  shared_ptr<Expression> right;

  virtual string str() const;
  BinaryOperation(BinaryOperator, shared_ptr<Expression>, shared_ptr<Expression>);
  BinaryOperation();
  virtual void accept(ASTVisitor* v);
};

enum TernaryOperator {
  // logical operators
  IfElseOperator = 0,

  InvalidTernaryOperation,
};

struct TernaryOperation : Expression {
  TernaryOperator oper;
  shared_ptr<Expression> left;
  shared_ptr<Expression> center;
  shared_ptr<Expression> right;

  virtual string str() const;
  TernaryOperation(TernaryOperator, shared_ptr<Expression>, shared_ptr<Expression>, shared_ptr<Expression>);
  TernaryOperation();
  virtual void accept(ASTVisitor* v);
};

struct ListConstructor : Expression {
  vector<shared_ptr<Expression> > items;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DictConstructor : Expression {
  vector<pair<shared_ptr<Expression>, shared_ptr<Expression> > > items;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct SetConstructor : Expression {
  vector<shared_ptr<Expression> > items;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TupleConstructor : Expression {
  vector<shared_ptr<Expression> > items;

  virtual string str() const;
  virtual bool valid_lvalue() const;
  virtual void accept(ASTVisitor* v);
};

struct ListComprehension : Expression {
  // [item_pattern for variables in source_data]
  shared_ptr<Expression> item_pattern;
  shared_ptr<UnpackingFormat> variables;
  shared_ptr<Expression> source_data;
  shared_ptr<Expression> if_expr;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DictComprehension : Expression {
  // {key_pattern: value_pattern for variables in source_data}
  shared_ptr<Expression> key_pattern;
  shared_ptr<Expression> value_pattern;
  shared_ptr<UnpackingFormat> variables;
  shared_ptr<Expression> source_data;
  shared_ptr<Expression> if_expr;

  virtual string str() const;
  DictComprehension(shared_ptr<Expression>, shared_ptr<Expression>, shared_ptr<UnpackingFormat>, shared_ptr<Expression>, shared_ptr<Expression>);
  virtual void accept(ASTVisitor* v);
};

struct SetComprehension : Expression {
  // {item_pattern for variables in source_data}
  shared_ptr<Expression> item_pattern;
  shared_ptr<UnpackingFormat> variables;
  shared_ptr<Expression> source_data;
  shared_ptr<Expression> if_expr;

  virtual string str() const;
  SetComprehension(shared_ptr<Expression>, shared_ptr<UnpackingFormat>, shared_ptr<Expression>, shared_ptr<Expression>);
  virtual void accept(ASTVisitor* v);
};

struct LambdaDefinition : Expression {
  vector<shared_ptr<ArgumentDefinition> > args;
  shared_ptr<Expression> result;

  virtual string str() const;
  LambdaDefinition(const vector<shared_ptr<ArgumentDefinition> >&, shared_ptr<Expression>);
  LambdaDefinition();
  virtual void accept(ASTVisitor* v);
};

struct FunctionCall : Expression {
  shared_ptr<Expression> function;
  vector<shared_ptr<ArgumentDefinition> > args;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ArrayIndex : Expression {
  shared_ptr<Expression> array;
  shared_ptr<Expression> index;

  virtual string str() const;
  virtual bool valid_lvalue() const;
  virtual void accept(ASTVisitor* v);
};

struct ArraySlice : Expression {
  shared_ptr<Expression> array;
  shared_ptr<Expression> slice_left;
  shared_ptr<Expression> slice_right;
  // TODO: step argument

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct IntegerConstant : Expression {
  long value;

  virtual string str() const;
  IntegerConstant(long);
  virtual void accept(ASTVisitor* v);
};

struct FloatingConstant : Expression {
  double value;

  virtual string str() const;
  FloatingConstant(float);
  virtual void accept(ASTVisitor* v);
};

struct StringConstant : Expression {
  string value;

  virtual string str() const;
  StringConstant(string);
  virtual void accept(ASTVisitor* v);
};

struct TrueConstant : Expression {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FalseConstant : Expression {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct NoneConstant : Expression {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct VariableLookup : Expression {
  string name;

  virtual string str() const;
  virtual bool valid_lvalue() const;
  VariableLookup(string);
  virtual void accept(ASTVisitor* v);
};

struct AttributeLookup : Expression {
  shared_ptr<Expression> left;
  shared_ptr<Expression> right;

  virtual string str() const;
  virtual bool valid_lvalue() const;
  AttributeLookup(shared_ptr<Expression>, shared_ptr<Expression>);
  AttributeLookup();
  virtual void accept(ASTVisitor* v);
};



struct Statement {
  // virtual
  virtual void print(int indent_level) const = 0;
  virtual string str() const = 0;
  virtual void accept(ASTVisitor* v) = 0;
};

struct SimpleStatement : Statement {
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v) = 0;
};

struct CompoundStatement : Statement {
  // virtual
  vector<shared_ptr<Statement> > suite;

  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v) = 0;

  void append_statement(const Statement& st);
};

struct ModuleStatement : CompoundStatement {
  virtual string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct ExpressionStatement : SimpleStatement {
  shared_ptr<Expression> expr;

  virtual string str() const;
  ExpressionStatement(shared_ptr<Expression>);
  virtual void accept(ASTVisitor* v);
};

struct AssignmentStatement : SimpleStatement {
  vector<shared_ptr<Expression> > left;
  vector<shared_ptr<Expression> > right;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

enum AugmentOperator {
  PlusEqualsOperator,
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
  // this isn't the same as AssignmentStatement since the latter will support chaining in the future maybe
  AugmentOperator oper;
  vector<shared_ptr<Expression> > left;
  vector<shared_ptr<Expression> > right;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct PrintStatement : SimpleStatement {
  shared_ptr<Expression> stream;
  vector<shared_ptr<Expression> > items;
  bool suppress_newline;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DeleteStatement : SimpleStatement {
  vector<shared_ptr<Expression> > items;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct PassStatement : SimpleStatement {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FlowStatement : SimpleStatement {
  // virtual
};

struct ImportStatement : SimpleStatement {
  vector<string> module_names; // import x, y
  vector<string> module_renames; // import x as y
  vector<string> symbol_list; // from x import y1, y2
  vector<string> symbol_renames; // from x import y1, y2 as z1, z2
  bool import_star; // from x import *

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct GlobalStatement : SimpleStatement {
  vector<string> names;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ExecStatement : SimpleStatement {
  shared_ptr<Expression> code;
  shared_ptr<Expression> globals;
  shared_ptr<Expression> locals;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct AssertStatement : SimpleStatement {
  shared_ptr<Expression> check;
  shared_ptr<Expression> failure_message;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct BreakStatement : FlowStatement {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ContinueStatement : FlowStatement {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ReturnStatement : FlowStatement {
  vector<shared_ptr<Expression> > items;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct RaiseStatement : FlowStatement {
  shared_ptr<Expression> type;
  shared_ptr<Expression> value;
  shared_ptr<Expression> traceback;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct YieldStatement : FlowStatement {
  shared_ptr<Expression> expr; // can be NULL

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct SingleIfStatement : CompoundStatement {
  shared_ptr<Expression> check;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ElseStatement : CompoundStatement {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct IfStatement : SingleIfStatement {
  vector<shared_ptr<SingleIfStatement> > elifs;
  shared_ptr<ElseStatement> else_suite; // may be NULL

  virtual string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct ElifStatement : SingleIfStatement {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ForStatement : CompoundStatement {
  shared_ptr<UnpackingFormat> variables;
  vector<shared_ptr<Expression> > in_exprs;
  shared_ptr<ElseStatement> else_suite; // may be NULL

  virtual string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct WhileStatement : CompoundStatement {
  shared_ptr<Expression> condition;
  shared_ptr<ElseStatement> else_suite; // may be NULL

  virtual string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct ExceptStatement : CompoundStatement {
  shared_ptr<Expression> types; // can be NULL for default except clause
  string name;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FinallyStatement : CompoundStatement {
  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TryStatement : CompoundStatement {
  vector<shared_ptr<ExceptStatement> > excepts;
  shared_ptr<ElseStatement> else_suite; // may be NULL
  shared_ptr<FinallyStatement> finally_suite; // may be NULL

  virtual string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct WithStatement : CompoundStatement {
  vector<shared_ptr<Expression> > items;
  vector<string> names;

  virtual string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FunctionDefinition : CompoundStatement {
  string name;
  vector<shared_ptr<ArgumentDefinition> > args;
  vector<shared_ptr<Expression> > decorators;

  virtual string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

struct ClassDefinition : CompoundStatement {
  string name;
  vector<shared_ptr<Expression> > parent_types;
  vector<shared_ptr<Expression> > decorators;

  virtual string str() const;
  virtual void print(int indent_level) const;
  virtual void accept(ASTVisitor* v);
};

#endif // _AST_HH