#pragma once

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "PythonOperators.hh"



enum class ValueType {
  Indeterminate = 0, // unknown type

  // trivial types
  None,
  Bool,
  Int,
  Float,

  // built-in class types
  Bytes,
  Unicode,
  List,
  Tuple,
  Set,
  Dict,

  // static object types
  Function,
  Class,
  Instance,
  Module,

  // meta-types
  ExtensionTypeReference, // reference to a class extension type
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
    int64_t extension_type_index;
  };
  void* instance; // used for Instance only
  std::vector<Variable> extension_types;

  // unknown value constructors
  Variable();
  explicit Variable(ValueType type);
  Variable(ValueType type, const std::vector<Variable>& extension_types);
  Variable(ValueType type, std::vector<Variable>&& extension_types);

  // Bool
  Variable(ValueType type, bool bool_value);

  // Int/Function/Class
  Variable(ValueType type, int64_t int_value);

  // Float
  Variable(ValueType type, double float_value);

  // Bytes/Module
  Variable(ValueType type, const char* bytes_value, size_t size);
  Variable(ValueType type, const char* bytes_value);
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

  // Instance
  Variable(ValueType type, int64_t class_id, void* instance);

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
  bool types_equal(const Variable& other) const;
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

Variable compute_list_extension_type(
    const std::vector<std::shared_ptr<Variable>>& list_value,
    bool allow_indeterminate = true);
std::vector<Variable> compute_tuple_extension_type(
    const std::vector<std::shared_ptr<Variable>>& tuple_value);
Variable compute_set_extension_type(
    const std::unordered_set<Variable>& set_value,
    bool allow_indeterminate = true);
std::pair<Variable, Variable> compute_dict_extension_type(
    const std::unordered_map<Variable, std::shared_ptr<Variable>>& dict_value,
    bool allow_indeterminate = true);

Variable execute_unary_operator(UnaryOperator oper, const Variable& var);
Variable execute_binary_operator(BinaryOperator oper, const Variable& left,
    const Variable& right);
Variable execute_ternary_operator(TernaryOperator oper, const Variable& left,
    const Variable& center, const Variable& right);
