#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>



struct ASTVisitor; // forward declaration since the visitor type depends on types declared in this file



enum class UnaryOperator {
  LogicalNot = 0, // not
  Not,            // ~
  Positive,       // +
  Negative,       // -
  Yield,          // yield
};

enum class BinaryOperator {
  LogicalOr = 0,   // or
  LogicalAnd,      // and
  LessThan,        // <
  GreaterThan,     // >
  Equality,        // ==
  GreaterOrEqual,  // >=
  LessOrEqual,     // <=
  NotEqual,        // !=
  In,              // in
  NotIn,           // not in
  Is,              // is
  IsNot,           // is not
  Or,              // |
  And,             // &
  Xor,             // ^
  LeftShift,       // <<
  RightShift,      // >>
  Addition,        // +
  Subtraction,     // -
  Multiplication,  // *
  Division,        // /
  Modulus,         // %
  IntegerDivision, // //
  Exponentiation,  // **
};

enum class TernaryOperator {
  IfElse = 0, // x if y else z
};

enum class AugmentOperator {
  Addition = 0,    // +=
  Subtraction,     // -=
  Multiplication,  // *=
  Division,        // /=
  Modulus,         // %=
  And,             // &=
  Or,              // |=
  Xor,             // ^=
  LeftShift,       // <<=
  RightShift,      // >>=
  Exponentiation,  // **=
  IntegerDivision, // //=
  _AugmentOperatorCount,
};

BinaryOperator binary_operator_for_augment_operator(AugmentOperator oper);



struct ASTNode {
  size_t file_offset;

  ASTNode() = delete;
  ASTNode(size_t file_offset);

  virtual std::string str() const = 0;
  virtual void accept(ASTVisitor* v) = 0;
};

struct Expression : ASTNode {
  Expression(size_t file_offset);

  virtual bool valid_lvalue() const;
};



struct TypeAnnotation {
  std::string type_name;
  std::vector<std::shared_ptr<TypeAnnotation>> generic_arguments;
};



struct LValueReference : Expression {
  LValueReference(size_t file_offset);

  virtual bool valid_lvalue() const;

  virtual std::string str() const = 0;
  virtual void accept(ASTVisitor* v) = 0;
};

struct AttributeLValueReference : LValueReference {
  std::shared_ptr<Expression> base; // may be NULL for references to local vars
  std::string name;
  std::shared_ptr<TypeAnnotation> type_annotation;

  AttributeLValueReference(std::shared_ptr<Expression> base,
      const std::string& name, std::shared_ptr<TypeAnnotation> type_annotation,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ArrayIndexLValueReference : LValueReference {
  std::shared_ptr<Expression> array;
  std::shared_ptr<Expression> index;

  ArrayIndexLValueReference(std::shared_ptr<Expression> array,
      std::shared_ptr<Expression> index, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ArraySliceLValueReference : LValueReference {
  std::shared_ptr<Expression> array;
  std::shared_ptr<Expression> start_index;
  std::shared_ptr<Expression> end_index;
  std::shared_ptr<Expression> step_size;

  ArraySliceLValueReference(std::shared_ptr<Expression> array,
      std::shared_ptr<Expression> start_index,
      std::shared_ptr<Expression> end_index,
      std::shared_ptr<Expression> step_size, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TupleLValueReference : LValueReference {
  std::vector<std::shared_ptr<Expression>> items; // lvalue references

  TupleLValueReference(std::vector<std::shared_ptr<Expression>>&& items,
      size_t file_offset);

  virtual bool valid_lvalue() const; // recurs on each tuple item

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};



struct UnaryOperation : Expression {
  UnaryOperator oper;
  std::shared_ptr<Expression> expr;

  // annotations
  int64_t split_id; // only used if oper == YieldOperator

  UnaryOperation(UnaryOperator oper, std::shared_ptr<Expression> expr,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct BinaryOperation : Expression {
  BinaryOperator oper;
  std::shared_ptr<Expression> left;
  std::shared_ptr<Expression> right;

  BinaryOperation(BinaryOperator oper, std::shared_ptr<Expression> left,
      std::shared_ptr<Expression> right, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TernaryOperation : Expression {
  TernaryOperator oper;
  std::shared_ptr<Expression> left;
  std::shared_ptr<Expression> center;
  std::shared_ptr<Expression> right;

  TernaryOperation(TernaryOperator oper, std::shared_ptr<Expression> left,
      std::shared_ptr<Expression> center, std::shared_ptr<Expression> right,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ListConstructor : Expression {
  std::vector<std::shared_ptr<Expression>> items;

  ListConstructor(size_t file_offset);
  ListConstructor(std::vector<std::shared_ptr<Expression>>&& items,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DictConstructor : Expression {
  std::vector<std::pair<std::shared_ptr<Expression>, std::shared_ptr<Expression>>> items;

  DictConstructor(size_t file_offset);
  DictConstructor(std::vector<std::pair<std::shared_ptr<Expression>, std::shared_ptr<Expression>>>&& items,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct SetConstructor : Expression {
  std::vector<std::shared_ptr<Expression>> items;

  SetConstructor(std::vector<std::shared_ptr<Expression>>&& items,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TupleConstructor : Expression {
  std::vector<std::shared_ptr<Expression>> items;

  TupleConstructor(size_t file_offset);
  TupleConstructor(std::vector<std::shared_ptr<Expression>>&& items,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ListComprehension : Expression {
  // [item_pattern for variable in source_data if predicate]
  std::shared_ptr<Expression> item_pattern;
  std::shared_ptr<Expression> variable; // lvalue references
  std::shared_ptr<Expression> source_data;
  std::shared_ptr<Expression> predicate; // can be NULL

  ListComprehension(std::shared_ptr<Expression> item_pattern,
      std::shared_ptr<Expression> variable,
      std::shared_ptr<Expression> source_data,
      std::shared_ptr<Expression> predicate, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DictComprehension : Expression {
  // {key_pattern: value_pattern for variable in source_data if predicate}
  std::shared_ptr<Expression> key_pattern;
  std::shared_ptr<Expression> value_pattern;
  std::shared_ptr<Expression> variable; // lvalue references
  std::shared_ptr<Expression> source_data;
  std::shared_ptr<Expression> predicate; // can be NULL

  DictComprehension(std::shared_ptr<Expression> key_pattern,
      std::shared_ptr<Expression> value_pattern,
      std::shared_ptr<Expression> variable,
      std::shared_ptr<Expression> source_data,
      std::shared_ptr<Expression> predicate, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct SetComprehension : Expression {
  // {item_pattern for variable in source_data if predicatre}
  std::shared_ptr<Expression> item_pattern;
  std::shared_ptr<Expression> variable; // lvalue references
  std::shared_ptr<Expression> source_data;
  std::shared_ptr<Expression> predicate; // can be NULL

  SetComprehension(std::shared_ptr<Expression> item_pattern,
      std::shared_ptr<Expression> variable,
      std::shared_ptr<Expression> source_data,
      std::shared_ptr<Expression> predicate, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FunctionArguments {
  struct Argument {
    std::string name;
    std::shared_ptr<TypeAnnotation> type_annotation; // NULL if not given
    std::shared_ptr<Expression> default_value; // NULL for non-keyword args

    Argument(const std::string& name,
        std::shared_ptr<TypeAnnotation> type_annotation,
        std::shared_ptr<Expression> default_value);

    std::string str() const;
  };

  // guarantee: all positional arguments appear before keyword arguments in args
  std::vector<Argument> args;
  std::string varargs_name;
  std::string varkwargs_name;

  FunctionArguments(std::vector<Argument>&& args,
      const std::string& varargs_name, const std::string& varkwargs_name);

  std::string str() const;
};

struct LambdaDefinition : Expression {
  FunctionArguments args;
  std::shared_ptr<Expression> result;

  // annotations
  int64_t function_id;

  LambdaDefinition(FunctionArguments&& args, std::shared_ptr<Expression> result,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FunctionCall : Expression {
  std::shared_ptr<Expression> function;
  std::vector<std::shared_ptr<Expression>> args;
  std::unordered_map<std::string, std::shared_ptr<Expression>> kwargs;
  std::shared_ptr<Expression> varargs;
  std::shared_ptr<Expression> varkwargs;

  // annotations
  int64_t function_id;
  int64_t split_id;

  int64_t callee_function_id;

  FunctionCall(std::shared_ptr<Expression> function,
      std::vector<std::shared_ptr<Expression>>&& args,
      std::unordered_map<std::string, std::shared_ptr<Expression>>&& kwargs,
      std::shared_ptr<Expression> varargs,
      std::shared_ptr<Expression> varkwargs, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ArrayIndex : Expression {
  std::shared_ptr<Expression> array;
  std::shared_ptr<Expression> index;

  // annotations
  bool index_constant;
  int64_t index_value;

  ArrayIndex(std::shared_ptr<Expression> array,
      std::shared_ptr<Expression> index, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ArraySlice : Expression {
  std::shared_ptr<Expression> array;
  std::shared_ptr<Expression> start_index;
  std::shared_ptr<Expression> end_index;
  std::shared_ptr<Expression> step_size;

  ArraySlice(std::shared_ptr<Expression> array,
      std::shared_ptr<Expression> start_index,
      std::shared_ptr<Expression> end_index,
      std::shared_ptr<Expression> step_size, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct IntegerConstant : Expression {
  int64_t value;

  IntegerConstant(int64_t value, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FloatConstant : Expression {
  double value;

  FloatConstant(double value, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct BytesConstant : Expression {
  std::string value;

  BytesConstant(const std::string& value, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct UnicodeConstant : Expression {
  std::wstring value;

  UnicodeConstant(const std::wstring& value, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TrueConstant : Expression {
  TrueConstant(size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FalseConstant : Expression {
  FalseConstant(size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct NoneConstant : Expression {
  NoneConstant(size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct VariableLookup : Expression {
  std::string name;

  VariableLookup(const std::string& name, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct AttributeLookup : Expression {
  std::shared_ptr<Expression> base;
  std::string name;

  // annotations
  std::string base_module_name;

  AttributeLookup(std::shared_ptr<Expression> base, const std::string& name,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};


struct Statement : ASTNode {
  Statement(size_t file_offset);

  virtual void print(FILE* stream, size_t indent_level = 0) const = 0;
  virtual std::string str() const = 0;
  virtual void accept(ASTVisitor* v) = 0;
};

struct SimpleStatement : Statement {
  SimpleStatement(size_t file_offset);

  virtual void print(FILE* stream, size_t indent_level = 0) const;
  virtual void accept(ASTVisitor* v) = 0;
};

struct CompoundStatement : Statement {
  std::vector<std::shared_ptr<Statement>> items;

  CompoundStatement(std::vector<std::shared_ptr<Statement>>&& items,
      size_t file_offset);

  virtual void print(FILE* stream, size_t indent_level = 0) const;
  virtual void accept(ASTVisitor* v) = 0;
};

struct ModuleStatement : CompoundStatement {
  ModuleStatement(std::vector<std::shared_ptr<Statement>>&& items,
      size_t file_offset);

  virtual void print(FILE* stream, size_t indent_level = 0) const;
  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ExpressionStatement : SimpleStatement {
  std::shared_ptr<Expression> expr;

  ExpressionStatement(std::shared_ptr<Expression> expr, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct AssignmentStatement : SimpleStatement {
  std::shared_ptr<Expression> target; // lvalue reference
  std::shared_ptr<Expression> value;

  AssignmentStatement(std::shared_ptr<Expression> target,
      std::shared_ptr<Expression> value, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct AugmentStatement : SimpleStatement {
  AugmentOperator oper;
  std::shared_ptr<Expression> target; // lvalue reference
  std::shared_ptr<Expression> value;

  AugmentStatement(AugmentOperator oper, std::shared_ptr<Expression> target,
      std::shared_ptr<Expression> value, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct DeleteStatement : SimpleStatement {
  std::shared_ptr<Expression> items; // lvalue references

  DeleteStatement(std::shared_ptr<Expression> items, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct PassStatement : SimpleStatement {
  PassStatement(size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FlowStatement : SimpleStatement {
  FlowStatement(size_t file_offset);
};

struct ImportStatement : SimpleStatement {
  // 1. import name1 [as rename1], module_name2 [as rename2], ...
  //    this->modules is length 1 or more
  //    this->names is empty
  // 2. from name1 import symbol1 [as symbol_rename1], symbol2 [as symbol_rename2], ...
  //    this->modules is length 1
  //    this->names is length 1 or more
  // 3. from name1 import *
  //    this->modules is length 1
  //    this->names is empty and this->import_star is true

  std::unordered_map<std::string, std::string> modules;
  std::unordered_map<std::string, std::string> names;
  bool import_star;

  ImportStatement(std::unordered_map<std::string, std::string>&& modules,
      std::unordered_map<std::string, std::string>&& names,
      bool import_star, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct GlobalStatement : SimpleStatement {
  std::vector<std::string> names;

  GlobalStatement(std::vector<std::string>&& names, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ExecStatement : SimpleStatement {
  std::shared_ptr<Expression> code;
  std::shared_ptr<Expression> globals;
  std::shared_ptr<Expression> locals;

  ExecStatement(std::shared_ptr<Expression> code,
      std::shared_ptr<Expression> globals,
      std::shared_ptr<Expression> locals, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct AssertStatement : SimpleStatement {
  std::shared_ptr<Expression> check;
  std::shared_ptr<Expression> failure_message;

  AssertStatement(std::shared_ptr<Expression> check,
      std::shared_ptr<Expression> failure_message, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct BreakStatement : FlowStatement {
  BreakStatement(size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ContinueStatement : FlowStatement {
  ContinueStatement(size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ReturnStatement : FlowStatement {
  std::shared_ptr<Expression> value;

  ReturnStatement(std::shared_ptr<Expression> value, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct RaiseStatement : FlowStatement {
  std::shared_ptr<Expression> type;
  std::shared_ptr<Expression> value;
  std::shared_ptr<Expression> traceback;

  RaiseStatement(std::shared_ptr<Expression> type,
      std::shared_ptr<Expression> value, std::shared_ptr<Expression> traceback,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct YieldStatement : FlowStatement {
  std::shared_ptr<Expression> expr; // if NULL, yields None
  bool from;

  // annotations
  int64_t split_id;

  YieldStatement(std::shared_ptr<Expression> expr, bool from,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct SingleIfStatement : CompoundStatement {
  std::shared_ptr<Expression> check;

  // annotations
  bool always_true;
  bool always_false;

  SingleIfStatement(std::shared_ptr<Expression> check,
      std::vector<std::shared_ptr<Statement>>&& items, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ElseStatement : CompoundStatement {
  ElseStatement(std::vector<std::shared_ptr<Statement>>&& items,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ElifStatement : SingleIfStatement {
  ElifStatement(std::shared_ptr<Expression> check,
      std::vector<std::shared_ptr<Statement>>&& items, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct IfStatement : SingleIfStatement {
  std::vector<std::shared_ptr<ElifStatement>> elifs;
  std::shared_ptr<ElseStatement> else_suite; // may be NULL

  IfStatement(std::shared_ptr<Expression> check,
      std::vector<std::shared_ptr<Statement>>&& items,
      std::vector<std::shared_ptr<ElifStatement>>&& elifs,
      std::shared_ptr<ElseStatement> else_suite, size_t file_offset);

  virtual std::string str() const;
  virtual void print(FILE* stream, size_t indent_level = 0) const;
  virtual void accept(ASTVisitor* v);
};

struct ForStatement : CompoundStatement {
  std::shared_ptr<Expression> variable; // lvalue reference
  std::shared_ptr<Expression> collection;
  std::shared_ptr<ElseStatement> else_suite; // may be NULL

  ForStatement(std::shared_ptr<Expression> variable,
      std::shared_ptr<Expression> collection,
      std::vector<std::shared_ptr<Statement>>&& items,
      std::shared_ptr<ElseStatement> else_suite, size_t file_offset);

  virtual std::string str() const;
  virtual void print(FILE* stream, size_t indent_level = 0) const;
  virtual void accept(ASTVisitor* v);
};

struct WhileStatement : CompoundStatement {
  std::shared_ptr<Expression> condition;
  std::shared_ptr<ElseStatement> else_suite; // may be NULL

  WhileStatement(std::shared_ptr<Expression> condition,
      std::vector<std::shared_ptr<Statement>>&& items,
      std::shared_ptr<ElseStatement> else_suite, size_t file_offset);

  virtual std::string str() const;
  virtual void print(FILE* stream, size_t indent_level = 0) const;
  virtual void accept(ASTVisitor* v);
};

struct ExceptStatement : CompoundStatement {
  std::shared_ptr<Expression> types; // can be NULL for default except clause
  std::string name;

  // annotations
  std::unordered_set<int64_t> class_ids;

  ExceptStatement(std::shared_ptr<Expression> types, const std::string& name,
      std::vector<std::shared_ptr<Statement>>&& items, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FinallyStatement : CompoundStatement {
  FinallyStatement(std::vector<std::shared_ptr<Statement>>&& items,
      size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct TryStatement : CompoundStatement {
  std::vector<std::shared_ptr<ExceptStatement>> excepts;
  std::shared_ptr<ElseStatement> else_suite; // may be NULL
  std::shared_ptr<FinallyStatement> finally_suite; // may be NULL

  TryStatement(std::vector<std::shared_ptr<Statement>>&& items,
      std::vector<std::shared_ptr<ExceptStatement>>&& excepts,
      std::shared_ptr<ElseStatement> else_suite,
      std::shared_ptr<FinallyStatement> finally_suite, size_t file_offset);

  virtual std::string str() const;
  virtual void print(FILE* stream, size_t indent_level = 0) const;
  virtual void accept(ASTVisitor* v);
};

struct WithStatement : CompoundStatement {
  std::vector<std::pair<std::shared_ptr<Expression>, std::string>> item_to_name;

  WithStatement(std::vector<std::pair<std::shared_ptr<Expression>, std::string>>&& item_to_name,
      std::vector<std::shared_ptr<Statement>>&& items, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct FunctionDefinition : CompoundStatement {
  std::vector<std::shared_ptr<Expression>> decorators;
  std::string name;
  FunctionArguments args;
  std::shared_ptr<TypeAnnotation> return_type_annotation;

  // annotations
  int64_t function_id;

  FunctionDefinition(std::vector<std::shared_ptr<Expression>>&& decorators,
      const std::string& name, FunctionArguments&& args,
      std::shared_ptr<TypeAnnotation> return_type_annotation,
      std::vector<std::shared_ptr<Statement>>&& items, size_t file_offset);

  virtual std::string str() const;
  virtual void accept(ASTVisitor* v);
};

struct ClassDefinition : CompoundStatement {
  std::vector<std::shared_ptr<Expression>> decorators;
  std::string name;
  std::vector<std::shared_ptr<Expression>> parent_types;

  // annotations
  int64_t class_id;

  ClassDefinition(std::vector<std::shared_ptr<Expression>>&& decorators,
      const std::string& name, std::vector<std::shared_ptr<Expression>>&& parent_types,
      std::vector<std::shared_ptr<Statement>>&& items, size_t file_offset);

  virtual std::string str() const;
  virtual void print(FILE* stream, size_t indent_level = 0) const;
  virtual void accept(ASTVisitor* v);
};
