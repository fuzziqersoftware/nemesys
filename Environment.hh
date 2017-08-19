#pragma once

#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "PythonASTNodes.hh"



enum class ValueType {
  Indeterminate = 0,
  None,
  Bool,
  Int,
  Float,
  Bytes,
  Unicode,
  List,
  Tuple,
  Set,
  Dict,
  Function,
  Class,
  Module,
};

struct Variable {
  ValueType type;
  bool value_known;
  union {
    int64_t int_value; // also used for Bool
    double float_value;
    std::string* bytes_value; // also used for Module
    std::wstring* unicode_value;
    std::vector<std::shared_ptr<Variable>>* list_value; // also used for Tuple
    std::unordered_set<Variable>* set_value;
    std::unordered_map<Variable, std::shared_ptr<Variable>>* dict_value;
    int64_t function_id;
    int64_t class_id;
  };

  // unknown value constructors
  Variable(); // Indeterminate
  Variable(ValueType type); // any type, unknown value

  // known value constructors
  Variable(bool bool_value);
  Variable(int64_t int_value);
  Variable(double float_value);
  Variable(const uint8_t* bytes_value, bool is_module = false);
  Variable(const uint8_t* bytes_value, size_t size, bool is_module = false);
  Variable(const std::string& bytes_value, bool is_module = false);
  Variable(std::string&& bytes_value, bool is_module = false);
  Variable(const wchar_t* unicode_value);
  Variable(const wchar_t* unicode_value, size_t size);
  Variable(const std::wstring& unicode_value);
  Variable(std::wstring&& unicode_value);
  Variable(const std::vector<std::shared_ptr<Variable>>& list_value, bool is_tuple);
  Variable(std::vector<std::shared_ptr<Variable>>&& list_value, bool is_tuple);
  Variable(const std::unordered_set<Variable>& set_value);
  Variable(std::unordered_set<Variable>&& set_value);
  Variable(const std::unordered_map<Variable, std::shared_ptr<Variable>>& dict_value);
  Variable(std::unordered_map<Variable, std::shared_ptr<Variable>>&& dict_value);
  Variable(int64_t function_or_class_id, bool is_class);

  // copy/move constructors
  Variable(const Variable&);
  Variable(Variable&&);
  Variable& operator=(const Variable&);
  Variable& operator=(Variable&&);

  ~Variable();

  void clear_value();

  std::string str() const;

  bool truth_value() const;
  bool operator==(const Variable& other) const;
  bool operator!=(const Variable& other) const;
};

namespace std {
  template <>
  struct hash<Variable> {
    size_t operator()(const Variable& k) const;
  };
}

bool type_has_refcount(ValueType type);

std::string type_signature_for_variables(const std::vector<Variable>& vars);

Variable execute_unary_operator(UnaryOperator oper, const Variable& var);
Variable execute_binary_operator(BinaryOperator oper, const Variable& left,
    const Variable& right);
Variable execute_ternary_operator(TernaryOperator oper, const Variable& left,
    const Variable& center, const Variable& right);
