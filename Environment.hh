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
  std::vector<Variable> extension_types;

  // unknown value constructors
  Variable();
  Variable(ValueType type);
  Variable(ValueType type, const std::vector<Variable>& extension_types);
  Variable(ValueType type, std::vector<Variable>&& extension_types);

  // Bool
  Variable(ValueType type, bool bool_value);

  // Int/Function/Class
  Variable(ValueType type, int64_t int_value);

  // Float
  Variable(ValueType type, double float_value);

  // Bytes/Module
  Variable(ValueType type, const uint8_t* bytes_value, size_t size);
  Variable(ValueType type, const uint8_t* bytes_value);
  Variable(ValueType type, const std::string& bytes_value);
  Variable(ValueType type, std::string&& bytes_value);

  // Unicode
  Variable(ValueType type, const wchar_t* unicode_value, size_t size);
  Variable(ValueType type, const wchar_t* unicode_value);
  Variable(ValueType type, const std::wstring& unicode_value);
  Variable(ValueType type, std::wstring&& unicode_value);

  // List/Tuple (extension types auto-computed)
  Variable(ValueType type, const std::vector<std::shared_ptr<Variable>>& list_value);
  Variable(ValueType type, std::vector<std::shared_ptr<Variable>>&& list_value);

  // Set (extension types auto-computed)
  Variable(ValueType type, const std::unordered_set<Variable>& set_value);
  Variable(ValueType type, std::unordered_set<Variable>&& set_value);

  // Dict (extension types auto-computed)
  Variable(ValueType type, const std::unordered_map<Variable, std::shared_ptr<Variable>>& dict_value);
  Variable(ValueType type, std::unordered_map<Variable, std::shared_ptr<Variable>>&& dict_value);

  // copy/move constructors
  Variable(const Variable&);
  Variable(Variable&&);
  Variable& operator=(const Variable&);
  Variable& operator=(Variable&&);

  ~Variable();

  void clear_value();
  Variable type_only() const;

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

std::string type_signature_for_variables(const std::vector<Variable>& vars,
    bool allow_indeterminate = false);

Variable execute_unary_operator(UnaryOperator oper, const Variable& var);
Variable execute_binary_operator(BinaryOperator oper, const Variable& left,
    const Variable& right);
Variable execute_ternary_operator(TernaryOperator oper, const Variable& left,
    const Variable& center, const Variable& right);
