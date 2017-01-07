#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "ast.hh"

using namespace std;



enum PyValueType {
  PyValueUnbound = 0,
  PyValueNone,
  PyValueBoolean,
  PyValueInteger,
  PyValueFloat,
  PyValueString,
  PyValueList,
  PyValueTuple,
  PyValueSet,
  PyValueDict,
  PyValueObject,
  PyValueFunction,
  PyValueLambda,
  PyValueClass,
};

struct PyValue {
  PyValue();
  PyValue(bool);
  PyValue(long);
  PyValue(double);
  PyValue(const string&);
  PyValue(const vector<PyValue>&, bool);
  PyValue(const unordered_set<PyValue>&);
  PyValue(const unordered_map<PyValue, PyValue>&, bool);
  PyValue(CompoundStatement*);
  PyValue(LambdaDefinition*);
  PyValue(ClassDefinition*);
  ~PyValue();

  void set_none();

  // containment operators
  PyValue __in__(const PyValue& other) const; // in
  PyValue __contains__(const PyValue& other) const; // in

  // arithmetic operators
  PyValue __pos__() const; // + (unary)
  PyValue __neg__() const; // - (unary)
  PyValue __add__(const PyValue& other) const; // +
  PyValue __sub__(const PyValue& other) const; // -
  PyValue __mul__(const PyValue& other) const; // *
  PyValue __div__(const PyValue& other) const; // /
  PyValue __floordiv__(const PyValue& other) const; // //
  PyValue __pow__(const PyValue& other) const; // **
  PyValue __mod__(const PyValue& other) const; // %

  // bitwise operators
  PyValue __and__() const; // &&
  PyValue __inv__() const; // ~
  PyValue __xor__() const; // ^
  PyValue __lshift__() const; // <<
  PyValue __rshift__() const; // <<

  // logical operators
  PyValue __not__() const; // !
  PyValue __eq__(const PyValue& other) const; // ==
  PyValue __ne__(const PyValue& other) const; // !=
  PyValue __lt__(const PyValue& other) const; // <
  PyValue __le__(const PyValue& other) const; // <=
  PyValue __gt__(const PyValue& other) const; // >
  PyValue __ge__(const PyValue& other) const; // >=

  bool valid() const;
  bool is_truthy() const;
  PyValue numeric_cast() const;
  string str() const;

  PyValueType value_type;
  union {
    bool value_bool;
    long value_int;
    double value_float;
    string* value_string;
    vector<PyValue>* value_list;
    unordered_map<PyValue, PyValue>* value_dict; // also used for object attrs
    FunctionDefinition* value_function;
    LambdaDefinition* value_lambda;
    ClassDefinition* value_class;
  };
};



PyValue PyExecUnaryOperator(UnaryOperator oper, const PyValue& value);
PyValue PyExecBinaryOperator(BinaryOperator oper, const PyValue& left, const PyValue& right);
PyValue PyExecTernaryOperator(TernaryOperator oper, const PyValue& left, const PyValue& center, const PyValue& right);
