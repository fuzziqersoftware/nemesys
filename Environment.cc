#include "Environment.hh"

#include <inttypes.h>
#include <stdio.h>

#include <exception>
#include <memory>
#include <phosg/Strings.hh>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Types/Reference.hh"

using namespace std;



static vector<Variable> compute_list_extension_type(
    const vector<shared_ptr<Variable>>& list_value) {
  // lists must have an extension type! if it's empty, we can't know what the
  // extension type is, so we'll make it Indeterminate for now
  Variable extension_type = list_value.empty() ?
      Variable() : list_value[0]->type_only();

  // all items in the list must have the same type, but can be an extended type
  for (const auto& it : list_value) {
    if (extension_type != it->type_only()) {
      throw invalid_argument("list contains multiple types");
    }
  }

  vector<Variable> ret;
  ret.emplace_back(move(extension_type));
  return ret;
}

static vector<Variable> compute_tuple_extension_type(
    const vector<shared_ptr<Variable>>& tuple_value) {
  // a tuple's extension types are the types of ALL of the elements
  vector<Variable> ret;
  for (const auto& it : tuple_value) {
    ret.emplace_back(it->type_only());
  }
  return ret;
}

static vector<Variable> compute_set_extension_type(
    const unordered_set<Variable>& set_value) {
  // basically the same as for List

  Variable extension_type = set_value.empty() ?
      Variable() : set_value.begin()->type_only();
  for (const auto& it : set_value) {
    if (extension_type != it.type_only()) {
      throw invalid_argument("set contains multiple types");
    }
  }

  vector<Variable> ret;
  ret.emplace_back(move(extension_type));
  return ret;
}

static vector<Variable> compute_dict_extension_type(
    const unordered_map<Variable, shared_ptr<Variable>>& dict_value) {
  // basically the same as for List/Set, but we have a key and value type

  Variable key_type = dict_value.empty() ?
      Variable() : dict_value.begin()->first.type_only();
  Variable value_type = dict_value.empty() ?
      Variable() : dict_value.begin()->second->type_only();
  for (const auto& it : dict_value) {
    if (key_type != it.first.type_only()) {
      throw invalid_argument("dict contains multiple key types");
    }
    if (value_type != it.second->type_only()) {
      throw invalid_argument("dict contains multiple value types");
    }
  }

  vector<Variable> ret;
  ret.emplace_back(move(key_type));
  ret.emplace_back(move(value_type));
  return ret;
}



// Indeterminate
Variable::Variable() : type(ValueType::Indeterminate), value_known(false),
    int_value(0), instance(NULL) { }

// any type, unknown value (except None, whose value is always known)
Variable::Variable(ValueType type) : type(type),
    value_known(type == ValueType::None), int_value(0), instance(NULL) { }

// any extended type, unknown value (except None, whose value is always known)
Variable::Variable(ValueType type, const vector<Variable>& extension_types) :
    type(type), value_known(type == ValueType::None), int_value(0),
    instance(NULL), extension_types(extension_types) { }
Variable::Variable(ValueType type, vector<Variable>&& extension_types) :
    type(type), value_known(type == ValueType::None), int_value(0),
    instance(NULL), extension_types(move(extension_types)) { }

// Bool
Variable::Variable(ValueType type, bool bool_value) : type(type),
    value_known(true), int_value(bool_value), instance(NULL) {
  if (this->type != ValueType::Bool) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, bool)", type));
  }
}

// Int/Function/Class
Variable::Variable(ValueType type, int64_t int_value) : type(type),
    value_known(true), int_value(int_value), instance(NULL) {
  if ((this->type != ValueType::Int) &&
      (this->type != ValueType::Function) &&
      (this->type != ValueType::Class)) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, int64_t)", type));
  }
}

// Float
Variable::Variable(ValueType type, double float_value) : type(type),
    value_known(true), float_value(float_value), instance(NULL) {
  if (this->type != ValueType::Float) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, double)", type));
  }
}

// Bytes/Module
Variable::Variable(ValueType type, const char* bytes_value, size_t size) :
    type(type), value_known(true), bytes_value(new string(bytes_value, size)),
    instance(NULL) {
  if ((this->type != ValueType::Bytes) &&
      (this->type != ValueType::Module)) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, const char*, size_t)", type));
  }
}
Variable::Variable(ValueType type, const char* bytes_value) :
    Variable(type, bytes_value, strlen(bytes_value)) { }
Variable::Variable(ValueType type, const string& bytes_value) : type(type),
    value_known(true), bytes_value(new string(bytes_value)), instance(NULL) {
  if ((this->type != ValueType::Bytes) &&
      (this->type != ValueType::Module)) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, const char*)", type));
  }
}
Variable::Variable(ValueType type, string&& bytes_value) : type(type),
    value_known(true), bytes_value(new string(move(bytes_value))),
    instance(NULL) {
  if ((this->type != ValueType::Bytes) &&
      (this->type != ValueType::Module)) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, string&&)", type));
  }
}

// Unicode
Variable::Variable(ValueType type, const wchar_t* unicode_value, size_t size) :
    type(type), value_known(true),
    unicode_value(new wstring(unicode_value, size)), instance(NULL) {
  if (this->type != ValueType::Unicode) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, const wchar_t*, size_t)", type));
  }
}
Variable::Variable(ValueType type, const wchar_t* unicode_value) :
    Variable(type, unicode_value, wcslen(unicode_value)) { }
Variable::Variable(ValueType type, const wstring& unicode_value) : type(type),
    value_known(true), unicode_value(new wstring(unicode_value)),
    instance(NULL) {
  if (this->type != ValueType::Unicode) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, const wchar_t*)", type));
  }
}
Variable::Variable(ValueType type, wstring&& unicode_value) : type(type),
    value_known(true), unicode_value(new wstring(move(unicode_value))),
    instance(NULL) {
  if (this->type != ValueType::Unicode) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, wstring&&)", type));
  }
}

// List/Tuple
Variable::Variable(ValueType type,
    const vector<shared_ptr<Variable>>& list_value) : type(type),
    value_known(true),
    list_value(new vector<shared_ptr<Variable>>(list_value)),
    instance(NULL) {
  if ((this->type != ValueType::List) && (this->type != ValueType::Tuple)) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, const vector<...>& list_value)", type));
  }
  if (this->type == ValueType::Tuple) {
    this->extension_types = compute_tuple_extension_type(*this->list_value);
  } else {
    this->extension_types = compute_list_extension_type(*this->list_value);
  }
}
Variable::Variable(ValueType type, vector<shared_ptr<Variable>>&& list_value) :
    type(type), value_known(true),
    list_value(new vector<shared_ptr<Variable>>(move(list_value))),
    instance(NULL) {
  if ((this->type != ValueType::List) && (this->type != ValueType::Tuple)) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, vector<...>&& list_value)", type));
  }
  if (this->type == ValueType::Tuple) {
    this->extension_types = compute_tuple_extension_type(*this->list_value);
  } else {
    this->extension_types = compute_list_extension_type(*this->list_value);
  }
}

// Set
Variable::Variable(ValueType type, const unordered_set<Variable>& set_value) :
    type(type), value_known(true),
    set_value(new unordered_set<Variable>(set_value)),
    instance(NULL) {
  if (this->type != ValueType::Set) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, const unordered_set<...>&)", type));
  }
  this->extension_types = compute_set_extension_type(*this->set_value);
}
Variable::Variable(ValueType type, unordered_set<Variable>&& set_value) :
    type(type), value_known(true),
    set_value(new unordered_set<Variable>(move(set_value))),
    instance(NULL) {
  if (this->type != ValueType::Set) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, unordered_set<...>&&)", type));
  }
  this->extension_types = compute_set_extension_type(*this->set_value);
}

// Dict
Variable::Variable(ValueType type,
    const unordered_map<Variable, shared_ptr<Variable>>& dict_value) :
    type(type), value_known(true),
    dict_value(new unordered_map<Variable, shared_ptr<Variable>>(dict_value)),
    instance(NULL) {
  if (this->type != ValueType::Dict) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, const unordered_map<...>&)", type));
  }
  this->extension_types = compute_dict_extension_type(*this->dict_value);
}
Variable::Variable(ValueType type, unordered_map<Variable, shared_ptr<Variable>>&& dict_value) :
    type(type), value_known(true),
    dict_value(new unordered_map<Variable, shared_ptr<Variable>>(move(dict_value))),
    instance(NULL) {
  if (this->type != ValueType::Dict) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, unordered_map<...>&&)", type));
  }
  this->extension_types = compute_dict_extension_type(*this->dict_value);
}

// Instance
Variable::Variable(ValueType type, int64_t class_id, void* instance) :
    type(type), value_known(instance ? true : false), class_id(class_id),
    instance(instance) {
  if (!class_id) {
    throw invalid_argument("Instance objects must have a nonzero class_id");
  }
  if (this->type != ValueType::Instance) {
    throw invalid_argument(string_printf("incorrect construction: Variable(%d, int64_t, void*)", type));
  }
}

// copy/move constructors
Variable::Variable(const Variable& other) {
  *this = other;
}

Variable::Variable(Variable&& other) {
  *this = move(other);
}

Variable& Variable::operator=(const Variable& other) {
  if (this == &other) {
    return *this;
  }

  this->type = other.type;
  this->value_known = other.value_known;
  this->extension_types = other.extension_types;

  if (this->value_known) {
    switch (this->type) {
      case ValueType::Indeterminate:
      case ValueType::None:
        break; // nothing to do
      case ValueType::Bool:
      case ValueType::Int:
        this->int_value = other.int_value;
        break;
      case ValueType::Float:
        this->float_value = other.float_value;
        break;
      case ValueType::Bytes:
      case ValueType::Module:
        this->bytes_value = new string(*other.bytes_value);
        break;
      case ValueType::Unicode:
        this->unicode_value = new wstring(*other.unicode_value);
        break;
      case ValueType::List:
      case ValueType::Tuple:
        this->list_value = new vector<shared_ptr<Variable>>(*other.list_value);
        break;
      case ValueType::Set:
        this->set_value = new unordered_set<Variable>(*other.set_value);
        break;
      case ValueType::Dict:
        this->dict_value = new unordered_map<Variable, shared_ptr<Variable>>(*other.dict_value);
        break;
      case ValueType::Function:
        this->function_id = other.function_id;
        break;
      case ValueType::Instance:
        this->instance = other.instance;
        if (this->instance) {
          add_reference(this->instance);
        }
      case ValueType::Class:
        this->class_id = other.class_id;
        break;
    }
  } else {
    if (this->type == ValueType::Instance) {
      this->class_id = other.class_id;
      this->instance = NULL;
    }
  }

  return *this;
}

Variable& Variable::operator=(Variable&& other) {
  if (this == &other) {
    return *this;
  }

  this->type = other.type;
  this->value_known = other.value_known;
  this->extension_types = move(other.extension_types);

  if (this->value_known) {
    switch (this->type) {
      case ValueType::Indeterminate:
      case ValueType::None:
        break; // nothing to do
      case ValueType::Bool:
      case ValueType::Int:
        this->int_value = other.int_value;
        break;
      case ValueType::Float:
        this->float_value = other.float_value;
        break;
      case ValueType::Bytes:
      case ValueType::Module:
        this->bytes_value = other.bytes_value;
        break;
      case ValueType::Unicode:
        this->unicode_value = other.unicode_value;
        break;
      case ValueType::List:
      case ValueType::Tuple:
        this->list_value = other.list_value;
        break;
      case ValueType::Set:
        this->set_value = other.set_value;
        break;
      case ValueType::Dict:
        this->dict_value = other.dict_value;
        break;
      case ValueType::Function:
        this->function_id = other.function_id;
        break;
      case ValueType::Instance:
        this->instance = other.instance;
        other.instance = NULL;
      case ValueType::Class:
        this->class_id = other.class_id;
        break;
    }
  } else {
    if (this->type == ValueType::Instance) {
      this->class_id = other.class_id;
      this->instance = NULL;
    }
  }

  other.type = ValueType::Indeterminate;
  other.value_known = false;
  // no need to reset the union contents; it is assumed to be garbage anyway if
  // the type is Indeterminate

  return *this;
}

Variable::~Variable() {
  this->clear_value();
}

void Variable::clear_value() {
  if (!this->value_known) {
    return;
  }
  this->value_known = false;

  switch (this->type) {
    case ValueType::Indeterminate:
    case ValueType::None:
    case ValueType::Bool:
    case ValueType::Int:
    case ValueType::Float:
    case ValueType::Function:
    case ValueType::Class:
      break; // nothing to do
    case ValueType::Bytes:
    case ValueType::Module:
      delete this->bytes_value;
      break;
    case ValueType::Unicode:
      delete this->unicode_value;
      break;
    case ValueType::List:
    case ValueType::Tuple:
      delete this->list_value;
      break;
    case ValueType::Set:
      delete this->set_value;
      break;
    case ValueType::Dict:
      delete this->dict_value;
      break;
    case ValueType::Instance:
      delete_reference(this->instance);
      this->instance = NULL;
  }
}

Variable Variable::type_only() const {
  Variable ret = *this;
  ret.clear_value();
  return ret;
}

string Variable::str() const {
  switch (this->type) {
    case ValueType::Indeterminate:
      return "Indeterminate";

    case ValueType::None:
      return "None";

    case ValueType::Bool:
      if (this->value_known) {
        return this->int_value ? "True" : "False";
      }
      return "Bool";

    case ValueType::Int:
      if (this->value_known) {
        return string_printf("%" PRId64, this->int_value);
      }
      return "Int";

    case ValueType::Float:
      if (this->value_known) {
        return string_printf("%lg", this->float_value);
      }
      return "Int";

    case ValueType::Bytes:
      if (this->value_known) {
        string ret = "b\'";
        for (char ch : *this->bytes_value) {
          if ((ch < 0x20) || (ch > 0x7E) || (ch == '\'')) {
            ret += string_printf("\\x%0" PRId8, ch);
          } else {
            ret += ch;
          }
        }
        ret += "\'";
        return ret;
      }
      return "Bytes";

    case ValueType::Unicode:
      if (this->value_known) {
        string ret = "\'";
        for (wchar_t ch : *this->unicode_value) {
          if ((ch < 0x20) || (ch > 0x7E) || (ch == '\'')) {
            ret += string_printf("\\x%0" PRId16, ch);
          } else {
            ret += ch;
          }
        }
        ret += "\'";
        return ret;
      }
      return "Unicode";

    case ValueType::List:
      if (this->value_known) {
        string ret = "[";
        for (const auto& item : *this->list_value) {
          if (ret.size() > 1) {
            ret += ", ";
          }
          ret += item->str();
        }
        return ret + "]";
      }
      return "List";

    case ValueType::Tuple:
      if (this->value_known) {
        string ret = "(";
        for (const auto& item : *this->list_value) {
          if (ret.size() > 1) {
            ret += ", ";
          }
          ret += item->str();
        }
        if (this->list_value->size() == 1) {
          ret += ",";
        }
        return ret + ")";
      }
      return "Tuple";

    case ValueType::Set:
      if (this->value_known) {
        string ret = "{";
        for (const auto& item : *this->set_value) {
          if (ret.size() > 1) {
            ret += ", ";
          }
          ret += item.str();
        }
        return ret + "}";
      }
      return "Set";

    case ValueType::Dict:
      if (this->value_known) {
        string ret = "{";
        for (const auto& item : *this->dict_value) {
          if (ret.size() > 1) {
            ret += ", ";
          }
          ret += item.first.str();
          ret += ": ";
          ret += item.second->str();
        }
        return ret + "}";
      }
      return "Dict";

    case ValueType::Function:
      if (!value_known) {
        return "Function";
      }
      return string_printf("Function:%" PRId64, this->function_id);

    case ValueType::Class:
      if (!value_known) {
        return "Class";
      }
      return string_printf("Class:%" PRId64, this->class_id);

    case ValueType::Instance:
      if (!value_known) {
        return string_printf("Instance:%" PRId64, this->class_id);
      }
      return string_printf("Instance:%" PRId64 "@%p", this->class_id, this->instance);

    case ValueType::Module:
      if (!value_known) {
        return "Module";
      }
      return string_printf("Module:%s", this->bytes_value->c_str());

    default:
      return "InvalidValueType";
  }
}

bool Variable::truth_value() const {
  switch (this->type) {
    case ValueType::Indeterminate:
      throw logic_error("variable with Indeterminate type has no truth value");
    case ValueType::None:
      return false;
    case ValueType::Bool:
      return this->int_value;
    case ValueType::Int:
      return (this->int_value != 0);
    case ValueType::Float:
      return (this->float_value != 0.0);
    case ValueType::Bytes:
      return !this->bytes_value->empty();
    case ValueType::Unicode:
      return !this->unicode_value->empty();
    case ValueType::List:
    case ValueType::Tuple:
      return !this->list_value->empty();
    case ValueType::Set:
      return !this->set_value->empty();
    case ValueType::Dict:
      return !this->dict_value->empty();
    case ValueType::Function:
    case ValueType::Class:
    case ValueType::Instance:
    case ValueType::Module:
      return true;
    default:
      throw logic_error(string_printf("variable has invalid type for truth test: 0x%" PRIX64,
          static_cast<int64_t>(this->type)));
  }
}



bool Variable::types_equal(const Variable& other) const {
  return (this->type == other.type) &&
         (this->extension_types == other.extension_types);
}

bool Variable::operator==(const Variable& other) const {
  if ((this->type != other.type) || (this->value_known != other.value_known)) {
    return false;
  }
  if (!this->value_known) {
    return true; // types match, values are unknown
  }
  switch (this->type) {
    case ValueType::None:
      return true;
    case ValueType::Bool:
    case ValueType::Int:
      return this->int_value == other.int_value;
    case ValueType::Float:
      return this->float_value == other.float_value;
    case ValueType::Bytes:
    case ValueType::Module:
      return *this->bytes_value == *other.bytes_value;
    case ValueType::Unicode:
      return *this->unicode_value == *other.unicode_value;
    case ValueType::List:
    case ValueType::Tuple:
      return *this->list_value == *other.list_value;
    case ValueType::Set:
      return *this->set_value == *other.set_value;
    case ValueType::Dict:
      return *this->dict_value == *other.dict_value;
    case ValueType::Function:
      return this->function_id == other.function_id;
    case ValueType::Class:
      return this->class_id == other.class_id;
    case ValueType::Instance:
      return (this->class_id == other.class_id) &&
             (this->instance == other.instance);
    default:
      throw logic_error(string_printf("variable has invalid type for equality check: 0x%" PRIX64,
          static_cast<int64_t>(this->type)));
  }
}

bool Variable::operator!=(const Variable& other) const {
  return !this->operator==(other);
}



bool type_has_refcount(ValueType type) {
  return (type != ValueType::Indeterminate) &&
         (type != ValueType::None) &&
         (type != ValueType::Bool) &&
         (type != ValueType::Int) &&
         (type != ValueType::Float) &&
         (type != ValueType::Function) &&
         (type != ValueType::Class) &&
         (type != ValueType::Module);
}



std::string type_signature_for_variables(const vector<Variable>& vars,
    bool allow_indeterminate) {
  string ret;
  for (const Variable& var : vars) {
    switch (var.type) {
      case ValueType::Indeterminate:
        if (allow_indeterminate) {
          ret += '?';
          break;
        }
        throw invalid_argument("cannot generate type signature for Indeterminate value");

      case ValueType::None:
        ret += 'n';
        break;

      case ValueType::Bool:
        ret += 'b';
        break;

      case ValueType::Int:
        ret += 'i';
        break;

      case ValueType::Float:
        ret += 'f';
        break;

      case ValueType::Bytes:
        ret += 'B';
        break;

      case ValueType::Module:
        ret += 'M';
        break;

      case ValueType::Unicode:
        ret += 'U';
        break;

      case ValueType::List:
        ret += 'L';
        if (var.extension_types.size() != 1) {
          throw invalid_argument("list does not have exactly one extension type");
        }
        ret += type_signature_for_variables(var.extension_types, allow_indeterminate);
        break;

      case ValueType::Tuple:
        // TODO
        throw invalid_argument("type signatures for Tuples not implemented");

      case ValueType::Set:
        // TODO
        throw invalid_argument("type signatures for Sets not implemented");

      case ValueType::Dict:
        ret += 'D';
        if (var.extension_types.size() != 2) {
          throw invalid_argument("dict does not have exactly two extension types");
        }
        ret += type_signature_for_variables(var.extension_types, allow_indeterminate);
        break;

      case ValueType::Function:
        ret += 'F';
        break;

      case ValueType::Instance:
        ret += string_printf("I%" PRId64, var.class_id);
        break;

      case ValueType::Class:
        // TODO
        throw invalid_argument("type signatures for Classes not implemented");

      default:
        throw logic_error(string_printf("variable has invalid type for type signature: 0x%" PRIX64,
            static_cast<int64_t>(var.type)));
    }
  }

  return ret;
}



namespace std {
  size_t hash<Variable>::operator()(const Variable& var) const {
    size_t h = hash<ValueType>()(var.type);
    if (var.type == ValueType::None) {
      return h;
    }

    if (!var.value_known) {
      // for unknown values, use the hash of the pointer... not great, but it
      // prevents them from all hashing tothe same bucket
      return h ^ hash<const Variable*>()(&var);
    }

    switch (var.type) {
      case ValueType::Bool:
      case ValueType::Int:
        return h ^ hash<int64_t>()(var.int_value);
      case ValueType::Float:
        return h ^ hash<float>()(var.float_value);
      case ValueType::Bytes:
      case ValueType::Module:
        return h ^ hash<string>()(*var.bytes_value);
      case ValueType::Unicode:
        return h ^ hash<wstring>()(*var.unicode_value);
      case ValueType::Tuple:
        for (const auto& it : *var.list_value) {
          h ^= hash<Variable>()(*it);
        }
        return h;
      case ValueType::Function:
        return h ^ hash<int64_t>()(var.class_id);
      case ValueType::Class:
        return h ^ hash<int64_t>()(var.function_id);
      default:
        throw logic_error(string_printf("variable has invalid type for hashing: 0x%" PRIX64,
            static_cast<int64_t>(var.type)));
    }
  }
}



Variable execute_unary_operator(UnaryOperator oper, const Variable& var) {
  switch (oper) {
    case UnaryOperator::LogicalNot:
      if (!var.value_known) {
        return Variable(ValueType::Bool); // also unknown, but it's a bool
      }
      return Variable(ValueType::Bool, !var.truth_value());

    case UnaryOperator::Not: {
      // this operator only works on bools and ints
      if (var.type == ValueType::Bool) {
        if (var.value_known) {
          if (var.int_value) {
            return Variable(ValueType::Int, static_cast<int64_t>(-2));
          } else {
            return Variable(ValueType::Int, static_cast<int64_t>(-1));
          }
        } else {
          return Variable(ValueType::Int);
        }
      }

      if (var.type != ValueType::Int) {
        if (var.value_known) {
          return Variable(ValueType::Int, ~var.int_value);
        } else {
          return Variable(ValueType::Int);
        }
      }

      if (var.type == ValueType::Indeterminate) {
        return Variable(ValueType::Int); // assume it's the right type
      }

      string var_str = var.str();
      throw invalid_argument(string_printf("can\'t compute bitwise not of %s", var_str.c_str()));
    }

    case UnaryOperator::Positive: {
      // this operator only works on bools, ints, and floats
      // bools turn into ints; ints and floats are returned verbatim
      if (var.type == ValueType::Bool) {
        if (var.value_known) {
          if (var.int_value) {
            return Variable(ValueType::Int, static_cast<int64_t>(1));
          } else {
            return Variable(ValueType::Int, static_cast<int64_t>(0));
          }
        } else {
          return Variable(ValueType::Int);
        }
      }

      if ((var.type == ValueType::Int) or (var.type == ValueType::Float)) {
        return var;
      }

      if (var.type == ValueType::Indeterminate) {
        return Variable(ValueType::Indeterminate);
      }

      string var_str = var.str();
      throw invalid_argument(string_printf("can\'t compute arithmetic positive of %s", var_str.c_str()));
    }

    case UnaryOperator::Negative: {
      // this operator only works on bools, ints, and floats
      if (var.type == ValueType::Bool) {
        if (var.value_known) {
          if (var.int_value) {
            return Variable(ValueType::Int, static_cast<int64_t>(-1));
          } else {
            return Variable(ValueType::Int, static_cast<int64_t>(0));
          }
        } else {
          return Variable(ValueType::Int);
        }
      }

      if (var.type == ValueType::Int) {
        if (var.value_known) {
          return Variable(ValueType::Int, -var.int_value);
        } else {
          return Variable(ValueType::Int);
        }
      }
      if (var.type == ValueType::Float) {
        if (var.value_known) {
          return Variable(ValueType::Float, -var.float_value);
        } else {
          return Variable(ValueType::Float);
        }
      }

      if (var.type == ValueType::Indeterminate) {
        return Variable(ValueType::Indeterminate);
      }

      string var_str = var.str();
      throw invalid_argument(string_printf("can\'t compute arithmetic negative of %s", var_str.c_str()));
    }

    case UnaryOperator::Yield:
      // this operator can return literally anything; it depends on the caller
      // let's just hope the value isn't used I guess
      return Variable(ValueType::Indeterminate);

    default:
      throw invalid_argument("unknown unary operator");
  }
}

Variable execute_binary_operator(BinaryOperator oper, const Variable& left,
    const Variable& right) {
  switch (oper) {
    case BinaryOperator::LogicalOr:
      // the result is the first argument if it's truthy, else the second
      // argument
      if (!left.value_known) {
        if ((left.type == ValueType::Function) || (left.type == ValueType::Class) || (left.type == ValueType::Module)) {
          return left; // left cannot be falsey
        }
        if (left.type == right.type) {
          return Variable(left.type); // we know the type, but not the value
        }
        return Variable(); // we don't even know the type
      }

      if (left.truth_value()) {
        return left;
      }
      return right;

    case BinaryOperator::LogicalAnd:
      // the result is the first argument if it's falsey, else the second
      // argument
      if (!left.value_known) {
        if ((left.type == ValueType::Function) || (left.type == ValueType::Class) || (left.type == ValueType::Module)) {
          return right; // left cannot be falsey
        }
        if (left.type == right.type) {
          return Variable(left.type); // we know the type, but not the value
        }
        return Variable(); // we don't even know the type
      }

      if (!left.truth_value()) {
        return left;
      }
      return right;

    case BinaryOperator::LessThan:
      // if we don't know even one of the values, we can't know the result
      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Bool);
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            return Variable(ValueType::Bool, left.int_value < right.int_value);
          }
          if (right.type == ValueType::Float) {
            return Variable(ValueType::Bool, left.int_value < right.float_value);
          }

          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compare %s < %s (left side integral; right side not numeric)", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Float: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            return Variable(ValueType::Bool, left.float_value < right.int_value);
          }
          if (right.type == ValueType::Float) {
            return Variable(ValueType::Bool, left.float_value < right.float_value);
          }

          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compare %s < %s (left side float; right side not numeric)", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Bytes: {
          if (right.type == ValueType::Bytes) {
            return Variable(ValueType::Bool, left.bytes_value < right.bytes_value);
          }

          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compare %s < %s (left side bytes; right side not bytes)", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Unicode: {
          if (right.type == ValueType::Unicode) {
            return Variable(ValueType::Bool, left.unicode_value < right.unicode_value);
          }

          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compare %s < %s (left side unicode; right side not unicode)", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::List:
        case ValueType::Tuple: {
          if (right.type == left.type) {
            size_t left_len = left.list_value->size();
            size_t right_len = right.list_value->size();
            for (size_t x = 0; x < min(left_len, right_len); x++) {
              Variable less_result = execute_binary_operator(
                  BinaryOperator::LessThan, *(*left.list_value)[x],
                  *(*right.list_value)[x]);
              if (!less_result.value_known) {
                return Variable(ValueType::Bool);
              }
              if (less_result.int_value) {
                return Variable(ValueType::Bool, true);
              }
              Variable greater_result = execute_binary_operator(
                  BinaryOperator::GreaterThan, *(*left.list_value)[x],
                  *(*right.list_value)[x]);
              if (!greater_result.value_known) {
                return Variable(ValueType::Bool);
              }
              if (greater_result.int_value) {
                return Variable(ValueType::Bool, false);
              }
            }
            return Variable(ValueType::Bool, left_len < right_len);
          }

          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compare %s < %s (left side list/tuple; right side not same type)", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Set:
          throw invalid_argument("subset operator not yet implemented");

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compare %s < %s (right side type not valid)", left_str.c_str(), right_str.c_str()));
        }
      }
      break;

    case BinaryOperator::Is:
      // it's unclear what we should do here, since the difference between Is
      // and Equality is an implementation detail. so I guess that means we can
      // do whatever we want? I'm going to make it be the same as Equality

    case BinaryOperator::Equality:
      // if we don't know both of the values, we can't know the result value
      // TODO: technically we could know the result value if the types are
      // different; implement this later (this is nontrivial because numeric
      // values can be equal across different value types)
      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Bool);
      }

      if ((left.type == ValueType::Bool) || (left.type == ValueType::Int)) {
        if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
          return Variable(ValueType::Bool, left.int_value == right.int_value);
        }
        if (right.type == ValueType::Float) {
          return Variable(ValueType::Bool, left.int_value == right.float_value);
        }
        return Variable(ValueType::Bool, false);
      }

      if (left.type == ValueType::Float) {
        if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
          return Variable(ValueType::Bool, left.float_value == right.int_value);
        }
        if (right.type == ValueType::Float) {
          return Variable(ValueType::Bool, left.float_value == right.float_value);
        }
        return Variable(ValueType::Bool, false);
      }

      // for all non-numeric types, the types must match exactly for equality
      if (right.type != left.type) {
        return Variable(ValueType::Bool, false);
      }

      switch (left.type) {
        case ValueType::Bytes:
          return Variable(ValueType::Bool, left.bytes_value == right.bytes_value);

        case ValueType::Unicode:
          return Variable(ValueType::Bool, left.unicode_value == right.unicode_value);

        case ValueType::List:
        case ValueType::Tuple: {
          size_t len = left.list_value->size();
          if (right.list_value->size() != len) {
            return Variable(ValueType::Bool, false);
          }

          for (size_t x = 0; x < len; x++) {
            Variable equal_result = execute_binary_operator(
                BinaryOperator::Equality, *(*left.list_value)[x],
                *(*right.list_value)[x]);
            if (!equal_result.value_known) {
              return Variable(ValueType::Bool);
            }
            if (!equal_result.int_value) {
              return Variable(ValueType::Bool, false);
            }
          }
          return Variable(ValueType::Bool, true);
        }

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compare %s == %s (this type has no equality operator)", left_str.c_str(), right_str.c_str()));
        }
      }
      break;

    case BinaryOperator::GreaterThan:
      return execute_unary_operator(UnaryOperator::LogicalNot,
          execute_binary_operator(BinaryOperator::LogicalOr,
            execute_binary_operator(BinaryOperator::LessThan, left, right),
            execute_binary_operator(BinaryOperator::Equality, left, right)));

    case BinaryOperator::GreaterOrEqual:
      return execute_unary_operator(UnaryOperator::LogicalNot,
          execute_binary_operator(BinaryOperator::LessThan, left, right));

    case BinaryOperator::LessOrEqual:
      return execute_binary_operator(BinaryOperator::LogicalOr,
          execute_binary_operator(BinaryOperator::LessThan, left, right),
          execute_binary_operator(BinaryOperator::Equality, left, right));

    case BinaryOperator::IsNot:
      // see comment in implementation for BinaryOperator::Is

    case BinaryOperator::NotEqual:
      return execute_unary_operator(UnaryOperator::LogicalNot,
          execute_binary_operator(BinaryOperator::Equality, left, right));

    case BinaryOperator::In:
      switch (right.type) {
        case ValueType::Indeterminate:
          return Variable(ValueType::Indeterminate);

        case ValueType::Bytes:
          if (left.type != ValueType::Bytes) {
            string left_str = left.str();
            string right_str = right.str();
            throw invalid_argument(string_printf("can\'t check inclusion of %s in %s (right side bytes; left side not bytes)", left_str.c_str(), right_str.c_str()));
          }

          if (left.value_known && left.bytes_value->empty()) {
            return Variable(ValueType::Bool, true);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }
          return Variable(ValueType::Bool,
              right.bytes_value->find(*left.bytes_value) != string::npos);

        case ValueType::Unicode:
          if (left.type != ValueType::Unicode) {
            string left_str = left.str();
            string right_str = right.str();
            throw invalid_argument(string_printf("can\'t check inclusion of %s in %s (right side unicode; left side not unicode)", left_str.c_str(), right_str.c_str()));
          }

          if (left.value_known && left.unicode_value->empty()) {
            return Variable(ValueType::Bool, true);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }
          return Variable(ValueType::Bool,
              right.unicode_value->find(*left.unicode_value) != string::npos);

        case ValueType::List:
        case ValueType::Tuple:
          if (right.value_known && right.list_value->empty()) {
            return Variable(ValueType::Bool, false);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }

          for (const auto& item : *right.list_value) {
            Variable equal_result = execute_binary_operator(
                BinaryOperator::Equality, left, *item);
            if (!equal_result.value_known) {
              return Variable(ValueType::Bool);
            }
            if (equal_result.int_value) {
              return Variable(ValueType::Bool, true);
            }
          }
          return Variable(ValueType::Bool, false);

        case ValueType::Set:
          if (right.value_known && right.set_value->empty()) {
            return Variable(ValueType::Bool, false);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }

          return Variable(ValueType::Bool,
              static_cast<bool>(right.set_value->count(left)));

        case ValueType::Dict:
          if (right.value_known && right.dict_value->empty()) {
            return Variable(ValueType::Bool, false);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }

          return Variable(ValueType::Bool,
              static_cast<bool>(right.dict_value->count(left)));

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t check inclusion of %s in %s (right side type invalid)", left_str.c_str(), right_str.c_str()));
        }
      }
      break;

    case BinaryOperator::NotIn:
      return execute_unary_operator(UnaryOperator::LogicalNot,
          execute_binary_operator(BinaryOperator::In, left, right));

    case BinaryOperator::Or:
      // handle set-union operation
      if ((left.type == ValueType::Set) && (right.type == ValueType::Set)) {
        if (left.value_known && right.value_known) {
          unordered_set<Variable> result = *left.set_value;
          for (const auto& item : *right.set_value) {
            result.emplace(item);
          }
          return Variable(ValueType::Set, move(result));
        } else {
          return Variable(ValueType::Set);
        }
      }

      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      // both sides must be Int or Bool; the result is Bool if both are Bool
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        string left_str = left.str();
        string right_str = right.str();
        throw invalid_argument(string_printf("can\'t compute bitwise or of %s and %s", left_str.c_str(), right_str.c_str()));
      }

      if ((left.type == ValueType::Bool) && (left.type == ValueType::Bool)) {
        if (left.value_known && left.int_value) {
          return Variable(ValueType::Bool, true);
        }
        if (right.value_known && right.int_value) {
          return Variable(ValueType::Bool, true);
        }
        if (!left.value_known || !right.value_known) {
          return Variable(ValueType::Bool);
        }
        return Variable(ValueType::Bool,
            static_cast<bool>(left.int_value || right.int_value));
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(ValueType::Int, left.int_value | right.int_value);

    case BinaryOperator::And:
      // handle set-intersection operation
      if ((left.type == ValueType::Set) && (right.type == ValueType::Set)) {
        if (left.value_known && right.value_known) {
          unordered_set<Variable> result = *left.set_value;
          for (auto it = result.begin(); it != result.end(); it++) {
            if (!right.set_value->count(*it)) {
              it = result.erase(it);
            } else {
              it++;
            }
          }
          return Variable(ValueType::Set, move(result));
        } else {
          return Variable(ValueType::Set);
        }
      }

      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      // both sides must be Int or Bool; the result is Bool if both are Bool
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        string left_str = left.str();
        string right_str = right.str();
        throw invalid_argument(string_printf("can\'t compute bitwise and of %s and %s", left_str.c_str(), right_str.c_str()));
      }

      if ((left.type == ValueType::Bool) && (left.type == ValueType::Bool)) {
        if (left.value_known && !left.int_value) {
          return Variable(ValueType::Bool, false);
        }
        if (right.value_known && !right.int_value) {
          return Variable(ValueType::Bool, false);
        }
        if (!left.value_known || !right.value_known) {
          return Variable(ValueType::Bool);
        }
        return Variable(ValueType::Bool, static_cast<bool>(left.int_value && right.int_value));
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(ValueType::Int, left.int_value & right.int_value);

    case BinaryOperator::Xor:
      // handle set-xor operation
      if ((left.type == ValueType::Set) && (right.type == ValueType::Set)) {
        if (left.value_known && right.value_known) {
          unordered_set<Variable> result = *left.set_value;
          for (const auto& item : *right.set_value) {
            if (!result.emplace(item).second) {
              result.erase(item);
            }
          }
          return Variable(ValueType::Set, move(result));
        } else {
          return Variable(ValueType::Set);
        }
      }

      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      // both sides must be Int or Bool; the result is Bool if both are Bool
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        string left_str = left.str();
        string right_str = right.str();
        throw invalid_argument(string_printf("can\'t compute xor of %s and %s", left_str.c_str(), right_str.c_str()));
      }

      if ((left.type == ValueType::Bool) && (left.type == ValueType::Bool)) {
        if (!left.value_known || !right.value_known) {
          return Variable(ValueType::Bool);
        }
        return Variable(ValueType::Bool,
            static_cast<bool>(left.int_value ^ right.int_value));
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(ValueType::Int, left.int_value ^ right.int_value);

    case BinaryOperator::LeftShift:
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      // both sides must be Int or Bool; the result is Int
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        string left_str = left.str();
        string right_str = right.str();
        throw invalid_argument(string_printf("can\'t compute left shift of %s by %s", left_str.c_str(), right_str.c_str()));
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(ValueType::Int, left.int_value << right.int_value);

    case BinaryOperator::RightShift:
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      // both sides must be Int or Bool; the result is Int
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        string left_str = left.str();
        string right_str = right.str();
        throw invalid_argument(string_printf("can\'t compute right shift of %s by %s", left_str.c_str(), right_str.c_str()));
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(ValueType::Int, left.int_value >> right.int_value);

    case BinaryOperator::Addition:
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Int);
            }
            return Variable(ValueType::Int, left.int_value + right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float, left.int_value + right.float_value);
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compute result of %s + %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Float: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float, left.float_value + right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float, left.float_value + right.float_value);
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compute result of %s + %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Bytes: {
          if (right.type == ValueType::Bytes) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Bytes);
            }
            string new_value = *left.bytes_value + *right.bytes_value;
            return Variable(ValueType::Bytes, move(new_value));
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compute result of %s + %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Unicode: {
          if (right.type == ValueType::Unicode) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Unicode);
            }
            wstring new_value = *left.unicode_value + *right.unicode_value;
            return Variable(ValueType::Unicode, move(new_value));
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compute result of %s + %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::List:
        case ValueType::Tuple: {
          if (right.type != left.type) {
            string left_str = left.str();
            string right_str = right.str();
            throw invalid_argument(string_printf("can\'t compute result of %s + %s", left_str.c_str(), right_str.c_str()));
          }

          vector<shared_ptr<Variable>> result = *left.list_value;
          result.insert(result.end(), right.list_value->begin(), right.list_value->end());
          return Variable(left.type, move(result));
        }

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t compute result of %s + %s", left_str.c_str(), right_str.c_str()));
        }
      }
      break;

    case BinaryOperator::Subtraction:
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      // handle set-difference operation
      if ((left.type == ValueType::Set) && (right.type == ValueType::Set)) {
        if (left.value_known && right.value_known) {
          unordered_set<Variable> result = *left.set_value;
          for (const auto& item : *right.set_value) {
            result.erase(item);
          }
          return Variable(ValueType::Set, move(result));
        } else {
          return Variable(ValueType::Set);
        }
      }

      // else, it's the same as left + (-right); just do that
      return execute_binary_operator(BinaryOperator::Addition, left,
          execute_unary_operator(UnaryOperator::Negative, right));

    case BinaryOperator::Multiplication: {
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      const Variable* list = NULL;
      const Variable* multiplier = NULL;
      if ((left.type == ValueType::List) || (left.type == ValueType::Tuple)) {
        list = &left;
        multiplier = &right;
      } else if ((right.type == ValueType::List) || (right.type == ValueType::Tuple)) {
        list = &right;
        multiplier = &left;
      }

      // if list isn't NULL, then it's valid and already typechecked, but we
      // need to typecheck multiplier
      if (list) {
        if ((multiplier->type != ValueType::Int) || (multiplier->type != ValueType::Bool)) {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t multiply %s by %s", left_str.c_str(), right_str.c_str()));
        }

        // short-circuit cases first
        if (list->value_known && list->list_value->empty()) {
          return Variable(list->type, vector<shared_ptr<Variable>>());
        }
        if (multiplier->value_known && (multiplier->int_value == 0)) {
          return Variable(list->type, vector<shared_ptr<Variable>>());
        }
        if (multiplier->value_known && (multiplier->int_value == 1)) {
          return *list;
        }
        if (!list->value_known || !multiplier->value_known) {
          return Variable(list->type);
        }

        vector<shared_ptr<Variable>> result;
        result.reserve(list->list_value->size() * multiplier->int_value);
        for (int64_t x = 0; x < multiplier->int_value; x++) {
          result.insert(result.end(), list->list_value->begin(), list->list_value->end());
        }
        return Variable(list->type, move(result));
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Int);
            }
            return Variable(ValueType::Int, left.int_value * right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float, left.int_value * right.float_value);
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t multiply %s by %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Float: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float, left.float_value * right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float, left.float_value * right.float_value);
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t multiply %s by %s", left_str.c_str(), right_str.c_str()));
        }

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t multiply %s by %s", left_str.c_str(), right_str.c_str()));
        }
      }
      break;
    }

    case BinaryOperator::Division:
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                static_cast<double>(left.int_value) /
                  static_cast<double>(right.int_value));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                static_cast<double>(left.int_value) / right.float_value);
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t divide %s by %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Float: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                left.float_value / static_cast<double>(right.int_value));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float, left.float_value / right.float_value);
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t divide %s by %s", left_str.c_str(), right_str.c_str()));
        }

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t divide %s by %s", left_str.c_str(), right_str.c_str()));
        }
      }
      break;

    case BinaryOperator::Modulus:
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      if (left.type == ValueType::Bytes) {
        // TODO
        return Variable(ValueType::Bytes);
      }
      if (left.type == ValueType::Unicode) {
        // TODO
        return Variable(ValueType::Unicode);
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Int);
            }
            return Variable(ValueType::Int, left.int_value % right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                fmod(static_cast<double>(left.int_value), right.float_value));
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t modulate %s by %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Float: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                fmod(left.float_value, static_cast<double>(right.int_value)));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                fmod(left.float_value, right.float_value));
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t modulate %s by %s", left_str.c_str(), right_str.c_str()));
        }

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t modulate %s by %s", left_str.c_str(), right_str.c_str()));
        }
      }
      break;

    case BinaryOperator::IntegerDivision:
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Int);
            }
            return Variable(ValueType::Int, left.int_value / right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                floor(static_cast<double>(left.int_value) / right.float_value));
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t integer-divide %s by %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Float: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                floor(left.float_value / static_cast<double>(right.int_value)));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                floor(left.float_value / right.float_value));
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t integer-divide %s by %s", left_str.c_str(), right_str.c_str()));
        }

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t integer-divide %s by %s", left_str.c_str(), right_str.c_str()));
        }
      }
      break;

    case BinaryOperator::Exponentiation:
      // if either side is Indeterminate, the result is Indeterminate
      if ((left.type == ValueType::Indeterminate) || (right.type == ValueType::Indeterminate)) {
        return Variable(ValueType::Indeterminate);
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (right.value_known && (right.int_value == 0)) {
              return Variable(ValueType::Int, static_cast<int64_t>(0));
            }
            if (left.value_known && (left.int_value == 1)) {
              return Variable(ValueType::Int, static_cast<int64_t>(1));
            }

            // TODO: this returns a Float if right is negative. for now we just
            // assume it's an Int if we don't know the right value, which could
            // be wrong :(
            if (!left.value_known) {
              if (right.value_known && (right.int_value < 0)) {
                return Variable(ValueType::Float);
              }
              return Variable(ValueType::Int);
            }
            if (!right.value_known) {
              return Variable(ValueType::Int);
            }

            if (right.int_value < 0) {
              return Variable(ValueType::Int,
                  pow(static_cast<double>(left.int_value),
                    static_cast<double>(right.float_value)));
            }

            // TODO: factor this out somewhere? it's basically the same as
            // what's in notes/pow.s
            int64_t ret = 1, base = left.int_value, exponent = right.int_value;
            for (; exponent > 0; exponent >>= 1) {
              if (exponent & 1) {
                ret *= base;
              }
              base *= base;
            }

            return Variable(ValueType::Int, ret);
          }

          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Int,
                pow(static_cast<double>(left.int_value), right.float_value));
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t exponentiate %s by %s", left_str.c_str(), right_str.c_str()));
        }

        case ValueType::Float: {
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                pow(left.float_value, static_cast<double>(right.int_value)));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(ValueType::Float,
                pow(left.float_value, right.float_value));
          }
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t exponentiate %s by %s", left_str.c_str(), right_str.c_str()));
        }

        default: {
          string left_str = left.str();
          string right_str = right.str();
          throw invalid_argument(string_printf("can\'t exponentiate %s by %s", left_str.c_str(), right_str.c_str()));
        }
      }
      break;

    default:
      throw invalid_argument("unknown binary operator");
  }
}

Variable execute_ternary_operator(TernaryOperator oper, const Variable& left,
    const Variable& center, const Variable& right) {
  if (oper != TernaryOperator::IfElse) {
    throw invalid_argument("invalid ternary operator");
  }

  if (center.value_known) {
    return center.truth_value() ? left : right;
  }

  Variable equal_result = execute_binary_operator(BinaryOperator::Equality,
      left, right);
  if (equal_result.value_known && equal_result.int_value) {
    return left;
  }

  if (left.type == right.type) {
    return Variable(left.type);
  }

  return Variable();
}
