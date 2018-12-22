#pragma once

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "Operators.hh"



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

struct Value {
  ValueType type;
  bool value_known;
  union {
    int64_t int_value; // also used for Bool
    double float_value;
    std::string* bytes_value; // also used for Module
    std::wstring* unicode_value;
    std::vector<std::shared_ptr<Value>>* list_value; // also used for Tuple
    std::unordered_set<Value>* set_value;
    std::unordered_map<Value, std::shared_ptr<Value>>* dict_value;
    int64_t function_id;
    int64_t class_id;
    int64_t extension_type_index;
  };
  void* instance; // used for Instance only
  std::vector<Value> extension_types;

  // unknown value constructors
  Value();
  explicit Value(ValueType type);
  Value(ValueType type, const std::vector<Value>& extension_types);
  Value(ValueType type, std::vector<Value>&& extension_types);

  // Bool
  Value(ValueType type, bool bool_value);

  // Int/Function/Class
  Value(ValueType type, int64_t int_value);

  // Float
  Value(ValueType type, double float_value);

  // Bytes/Module
  Value(ValueType type, const char* bytes_value, size_t size);
  Value(ValueType type, const char* bytes_value);
  Value(ValueType type, const std::string& bytes_value);
  Value(ValueType type, std::string&& bytes_value);

  // Unicode
  Value(ValueType type, const wchar_t* unicode_value, size_t size);
  Value(ValueType type, const wchar_t* unicode_value);
  Value(ValueType type, const std::wstring& unicode_value);
  Value(ValueType type, std::wstring&& unicode_value);

  // List/Tuple (extension types auto-computed)
  Value(ValueType type, const std::vector<std::shared_ptr<Value>>& list_value);
  Value(ValueType type, std::vector<std::shared_ptr<Value>>&& list_value);

  // Set (extension types auto-computed)
  Value(ValueType type, const std::unordered_set<Value>& set_value);
  Value(ValueType type, std::unordered_set<Value>&& set_value);

  // Dict (extension types auto-computed)
  Value(ValueType type, const std::unordered_map<Value, std::shared_ptr<Value>>& dict_value);
  Value(ValueType type, std::unordered_map<Value, std::shared_ptr<Value>>&& dict_value);

  // Instance
  Value(ValueType type, int64_t class_id, void* instance);

  // copy/move constructors
  Value(const Value&);
  Value(Value&&);
  Value& operator=(const Value&);
  Value& operator=(Value&&);

  ~Value();

  bool has_root_type() const;
  bool has_complete_type() const;
  bool has_value() const;

  void clear_value();
  Value type_only() const;

  std::string str() const;

  bool truth_value() const;
  bool types_equal(const Value& other) const;
  bool operator==(const Value& other) const;
  bool operator!=(const Value& other) const;
};

namespace std {
  template <>
  struct hash<Value> {
    size_t operator()(const Value& k) const;
  };
}

bool type_has_refcount(ValueType type);

std::string type_signature_for_variables(const std::vector<Value>& vars,
    bool allow_indeterminate = false);

Value compute_list_extension_type(
    const std::vector<std::shared_ptr<Value>>& list_value,
    bool allow_indeterminate = true);
std::vector<Value> compute_tuple_extension_type(
    const std::vector<std::shared_ptr<Value>>& tuple_value);
Value compute_set_extension_type(
    const std::unordered_set<Value>& set_value,
    bool allow_indeterminate = true);
std::pair<Value, Value> compute_dict_extension_type(
    const std::unordered_map<Value, std::shared_ptr<Value>>& dict_value,
    bool allow_indeterminate = true);

Value execute_unary_operator(UnaryOperator oper, const Value& var);
Value execute_binary_operator(BinaryOperator oper, const Value& left,
    const Value& right);
Value execute_ternary_operator(TernaryOperator oper, const Value& left,
    const Value& center, const Value& right);
