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

using namespace std;



static int64_t ipow(int64_t base, int64_t exponent) {
  // TODO: implement log-time exponentiation
  int64_t res = 1;
  for (; exponent > 0; exponent--) {
    res *= base;
  }
  return res;
}



Variable::Variable() : type(ValueType::Indeterminate), value_known(false) { }
Variable::Variable(ValueType type) : type(type), value_known(false) { }

Variable::Variable(bool bool_value) : type(ValueType::Bool),
    value_known(true), int_value(bool_value) { }
Variable::Variable(int64_t int_value) : type(ValueType::Int),
    value_known(true), int_value(int_value) { }
Variable::Variable(double float_value) : type(ValueType::Float),
    value_known(true), float_value(float_value) { }
Variable::Variable(const uint8_t* bytes_value, bool is_module) :
    type(is_module ? ValueType::Module : ValueType::Bytes),
    value_known(true), bytes_value(new string(reinterpret_cast<const char*>(bytes_value))) { }
Variable::Variable(const uint8_t* bytes_value, size_t size, bool is_module) :
    type(is_module ? ValueType::Module : ValueType::Bytes),
    value_known(true), bytes_value(new string(reinterpret_cast<const char*>(bytes_value), size)) { }
Variable::Variable(const string& bytes_value, bool is_module) :
    type(is_module ? ValueType::Module : ValueType::Bytes),
    value_known(true), bytes_value(new string(bytes_value)) { }
Variable::Variable(string&& bytes_value, bool is_module) :
    type(is_module ? ValueType::Module : ValueType::Bytes),
    value_known(true), bytes_value(new string(move(bytes_value))) { }
Variable::Variable(const wchar_t* unicode_value) : type(ValueType::Unicode),
    value_known(true), unicode_value(new wstring(unicode_value)) { }
Variable::Variable(const wchar_t* unicode_value, size_t size) : type(ValueType::Unicode),
    value_known(true), unicode_value(new wstring(unicode_value, size)) { }
Variable::Variable(const wstring& unicode_value) : type(ValueType::Unicode),
    value_known(true), unicode_value(new wstring(unicode_value)) { }
Variable::Variable(wstring&& unicode_value) : type(ValueType::Unicode),
    value_known(true), unicode_value(new wstring(move(unicode_value))) { }
Variable::Variable(const vector<shared_ptr<Variable>>& list_value, bool is_tuple) :
    type(is_tuple ? ValueType::Tuple : ValueType::List),
    value_known(true), list_value(new vector<shared_ptr<Variable>>(list_value)) { }
Variable::Variable(vector<shared_ptr<Variable>>&& list_value, bool is_tuple) :
    type(is_tuple ? ValueType::Tuple : ValueType::List),
    value_known(true), list_value(new vector<shared_ptr<Variable>>(move(list_value))) { }
Variable::Variable(const unordered_set<Variable>& set_value) : type(ValueType::Set),
    value_known(true), set_value(new unordered_set<Variable>(set_value)) { }
Variable::Variable(unordered_set<Variable>&& set_value) : type(ValueType::Set),
    value_known(true), set_value(new unordered_set<Variable>(move(set_value))) { }
Variable::Variable(const unordered_map<Variable, shared_ptr<Variable>>& dict_value) : type(ValueType::Dict),
    value_known(true), dict_value(new unordered_map<Variable, shared_ptr<Variable>>(dict_value)) { }
Variable::Variable(unordered_map<Variable, shared_ptr<Variable>>&& dict_value) : type(ValueType::Dict),
    value_known(true), dict_value(new unordered_map<Variable, shared_ptr<Variable>>(move(dict_value))) { }
Variable::Variable(uint64_t function_or_class_id, bool is_class) :
    type(is_class ? ValueType::Class : ValueType::Function),
    value_known(true), function_id(function_or_class_id) { }

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
      case ValueType::Class:
        this->class_id = other.class_id;
        break;
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
      case ValueType::Class:
        this->class_id = other.class_id;
        break;
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
  }
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
        for (wchar_t ch : *this->bytes_value) {
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
      return string_printf("Function:%" PRIu64, this->function_id);

    case ValueType::Class:
      return string_printf("Class:%" PRIu64, this->class_id);

    case ValueType::Module:
      return string_printf("Module:%s", this->bytes_value->c_str());

    default:
      return "InvalidValueType";
  }
}

bool Variable::truth_value() const {
  switch (this->type) {
    case ValueType::Indeterminate:
      throw logic_error("variable with indeterminate type has no truth value");
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
    case ValueType::Module:
      return true;
    default:
      throw logic_error("variable has invalid type");
  }
}



bool Variable::operator==(const Variable& other) const {
  if ((this->type != other.type) || !this->value_known || !other.value_known) {
    return false;
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
    default:
      throw logic_error("variable has invalid type");
  }
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
        return h ^ hash<uint64_t>()(var.class_id);
      case ValueType::Class:
        return h ^ hash<uint64_t>()(var.function_id);
      default:
        throw logic_error("variable has invalid type for hashing");
    }
  }
}



Variable execute_unary_operator(UnaryOperator oper, const Variable& var) {
  switch (oper) {
    case UnaryOperator::LogicalNot:
      if (!var.value_known) {
        return Variable(ValueType::Bool); // also unknown, but it's a bool
      }
      return Variable(!var.truth_value());

    case UnaryOperator::Not:
      // this operator only works on bools and ints
      if (var.type == ValueType::Bool) {
        if (var.value_known) {
          if (var.int_value) {
            return Variable(static_cast<int64_t>(-2));
          } else {
            return Variable(static_cast<int64_t>(-1));
          }
        } else {
          return Variable(ValueType::Int);
        }
      }

      if (var.type != ValueType::Int) {
        if (var.value_known) {
          return Variable(~var.int_value);
        } else {
          return Variable(ValueType::Int);
        }
      }

      throw invalid_argument("Not doesn\'t work on this type");

    case UnaryOperator::Positive:
      // this operator only works on bools, ints, and floats
      // bools turn into ints; ints and floats are returned verbatim
      if (var.type == ValueType::Bool) {
        if (var.value_known) {
          if (var.int_value) {
            return Variable(static_cast<int64_t>(1));
          } else {
            return Variable(static_cast<int64_t>(0));
          }
        } else {
          return Variable(ValueType::Int);
        }
      }

      if ((var.type == ValueType::Int) or (var.type == ValueType::Float)) {
        return var;
      }

      throw invalid_argument("Positive doesn\'t work on this type");

    case UnaryOperator::Negative:
      // this operator only works on bools, ints, and floats
      if (var.type == ValueType::Bool) {
        if (var.value_known) {
          if (var.int_value) {
            return Variable(static_cast<int64_t>(-1));
          } else {
            return Variable(static_cast<int64_t>(0));
          }
        } else {
          return Variable(ValueType::Int);
        }
      }

      if (var.type == ValueType::Int) {
        if (var.value_known) {
          return Variable(-var.int_value);
        } else {
          return Variable(ValueType::Int);
        }
      }
      if (var.type == ValueType::Float) {
        if (var.value_known) {
          return Variable(-var.float_value);
        } else {
          return Variable(ValueType::Float);
        }
      }

      throw invalid_argument("Negative doesn\'t work on this type");

    case UnaryOperator::Representation:
      // TODO: the parser should just convert these to function calls instead
      throw invalid_argument("`` operator not supported; use repr() instead");

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
        case ValueType::Int:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            return Variable(left.int_value < right.int_value);
          }
          if (right.type == ValueType::Float) {
            return Variable(left.int_value < right.float_value);
          }
          throw invalid_argument("can\'t compare numeric and non-numeric types");

        case ValueType::Float:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            return Variable(left.float_value < right.int_value);
          }
          if (right.type == ValueType::Float) {
            return Variable(left.float_value < right.float_value);
          }
          throw invalid_argument("can\'t compare numeric and non-numeric types");

        case ValueType::Bytes:
          if (right.type == ValueType::Bytes) {
            return Variable(left.bytes_value < right.bytes_value);
          }
          throw invalid_argument("can\'t compare bytes and non-bytes");

        case ValueType::Unicode:
          if (right.type == ValueType::Unicode) {
            return Variable(left.unicode_value < right.unicode_value);
          }
          throw invalid_argument("can\'t compare unicode and non-unicode");

        case ValueType::List:
        case ValueType::Tuple:
          if (right.type == left.type) {
            size_t left_len = left.list_value->size();
            size_t right_len = right.list_value->size();
            for (size_t x = 0; x < min(left_len, right_len); x++) {
              Variable less_result = execute_binary_operator(
                  BinaryOperator::LessThan, (*left.list_value)[x].get(),
                  (*right.list_value)[x].get());
              if (!less_result.value_known) {
                return Variable(ValueType::Bool);
              }
              if (less_result.int_value) {
                return Variable(true);
              }
              Variable greater_result = execute_binary_operator(
                  BinaryOperator::GreaterThan, (*left.list_value)[x].get(),
                  (*right.list_value)[x].get());
              if (!greater_result.value_known) {
                return Variable(ValueType::Bool);
              }
              if (greater_result.int_value) {
                return Variable(false);
              }
            }
            return Variable(left_len < right_len);
          }
          throw invalid_argument("can\'t compare list/tuple and non-list/tuple");

        case ValueType::Set:
          throw invalid_argument("subset operator not yet implemented");

        default:
          throw invalid_argument("invalid type to LessThan");
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
          return Variable(left.int_value == right.int_value);
        }
        if (right.type == ValueType::Float) {
          return Variable(left.int_value == right.float_value);
        }
        return Variable(false);
      }

      if (left.type == ValueType::Float) {
        if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
          return Variable(left.float_value == right.int_value);
        }
        if (right.type == ValueType::Float) {
          return Variable(left.float_value == right.float_value);
        }
        return Variable(false);
      }

      // for all non-numeric types, the types must match exactly for equality
      if (right.type != left.type) {
        return Variable(false);
      }

      switch (left.type) {
        case ValueType::Bytes:
          return Variable(left.bytes_value == right.bytes_value);

        case ValueType::Unicode:
          return Variable(left.unicode_value == right.unicode_value);

        case ValueType::List:
        case ValueType::Tuple: {
          size_t len = left.list_value->size();
          if (right.list_value->size() != len) {
            return Variable(false);
          }

          for (size_t x = 0; x < len; x++) {
            Variable equal_result = execute_binary_operator(
                BinaryOperator::Equality, (*left.list_value)[x].get(),
                (*right.list_value)[x].get());
            if (!equal_result.value_known) {
              return Variable(ValueType::Bool);
            }
            if (!equal_result.int_value) {
              return Variable(false);
            }
          }
          return Variable(true);
        }

        default:
          throw invalid_argument("invalid type to LessThan");
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
        case ValueType::Bytes:
          if (left.type != ValueType::Bytes) {
            throw invalid_argument("In Bytes requires another Bytes");
          }

          if (left.value_known && left.bytes_value->empty()) {
            return Variable(true);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }
          return Variable(right.bytes_value->find(*left.bytes_value) != string::npos);

        case ValueType::Unicode:
          if (left.type != ValueType::Unicode) {
            throw invalid_argument("In Unicode requires another Unicode");
          }

          if (left.value_known && left.unicode_value->empty()) {
            return Variable(true);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }
          return Variable(right.unicode_value->find(*left.unicode_value) != string::npos);

        case ValueType::List:
        case ValueType::Tuple:
          if (right.value_known && right.list_value->empty()) {
            return Variable(false);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }

          for (const auto& item : *right.list_value) {
            Variable equal_result = execute_binary_operator(
                BinaryOperator::Equality, left, item.get());
            if (!equal_result.value_known) {
              return Variable(ValueType::Bool);
            }
            if (equal_result.int_value) {
              return Variable(true);
            }
          }
          return Variable(false);

        case ValueType::Set:
          if (right.value_known && right.set_value->empty()) {
            return Variable(false);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }

          return Variable(static_cast<bool>(right.set_value->count(left)));

        case ValueType::Dict:
          if (right.value_known && right.dict_value->empty()) {
            return Variable(false);
          }
          if (!left.value_known || !right.value_known) {
            return Variable(ValueType::Bool);
          }

          return Variable(static_cast<bool>(right.dict_value->count(left)));

        default:
          throw invalid_argument("non-collection given to In");
      }
      break;

    case BinaryOperator::NotIn:
      return execute_unary_operator(UnaryOperator::LogicalNot,
          execute_binary_operator(BinaryOperator::In, left, right));

    case BinaryOperator::Or:
      // handle set-union operation
      if ((left.type == ValueType::Set) && (right.type == ValueType::Set)) {
        unordered_set<Variable> result = *left.set_value;
        for (const auto& item : *right.set_value) {
          result.emplace(item);
        }
        return Variable(move(result));
      }

      // both sides must be Int or Bool; the result is Bool if both are Bool
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        throw invalid_argument("Or requires integer/boolean arguments");
      }

      if ((left.type == ValueType::Bool) && (left.type == ValueType::Bool)) {
        if (left.value_known && left.int_value) {
          return Variable(true);
        }
        if (right.value_known && right.int_value) {
          return Variable(true);
        }
        if (!left.value_known || !right.value_known) {
          return Variable(ValueType::Bool);
        }
        return Variable(static_cast<bool>(left.int_value || right.int_value));
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(left.int_value | right.int_value);

    case BinaryOperator::And:
      // handle set-intersection operation
      if ((left.type == ValueType::Set) && (right.type == ValueType::Set)) {
        unordered_set<Variable> result = *left.set_value;
        for (auto it = result.begin(); it != result.end(); it++) {
          if (!right.set_value->count(*it)) {
            it = result.erase(it);
          } else {
            it++;
          }
        }
        return Variable(move(result));
      }

      // both sides must be Int or Bool; the result is Bool if both are Bool
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        throw invalid_argument("And requires integer/boolean arguments");
      }

      if ((left.type == ValueType::Bool) && (left.type == ValueType::Bool)) {
        if (left.value_known && !left.int_value) {
          return Variable(false);
        }
        if (right.value_known && !right.int_value) {
          return Variable(false);
        }
        if (!left.value_known || !right.value_known) {
          return Variable(ValueType::Bool);
        }
        return Variable(static_cast<bool>(left.int_value && right.int_value));
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(left.int_value & right.int_value);

    case BinaryOperator::Xor:
      // handle set-xor operation
      if ((left.type == ValueType::Set) && (right.type == ValueType::Set)) {
        unordered_set<Variable> result = *left.set_value;
        for (const auto& item : *right.set_value) {
          if (!result.emplace(item).second) {
            result.erase(item);
          }
        }
        return Variable(move(result));
      }

      // both sides must be Int or Bool; the result is Bool if both are Bool
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        throw invalid_argument("Xor requires integer/boolean arguments");
      }

      if ((left.type == ValueType::Bool) && (left.type == ValueType::Bool)) {
        if (!left.value_known || !right.value_known) {
          return Variable(ValueType::Bool);
        }
        return Variable(static_cast<bool>(left.int_value ^ right.int_value));
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(left.int_value ^ right.int_value);

    case BinaryOperator::LeftShift:
      // both sides must be Int or Bool; the result is Int
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        throw invalid_argument("LeftShift requires integer/boolean arguments");
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(left.int_value << right.int_value);

    case BinaryOperator::RightShift:
      // both sides must be Int or Bool; the result is Int
      if (((left.type != ValueType::Bool) && (left.type != ValueType::Int)) ||
          ((right.type != ValueType::Bool) && (right.type != ValueType::Int))) {
        throw invalid_argument("RightShift requires integer/boolean arguments");
      }

      if (!left.value_known || !right.value_known) {
        return Variable(ValueType::Int);
      }
      return Variable(left.int_value >> right.int_value);

    case BinaryOperator::Addition:
      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Int);
            }
            return Variable(left.int_value + right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(left.int_value + right.float_value);
          }
          throw invalid_argument("can\'t add numeric and non-numeric types");

        case ValueType::Float:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(left.float_value + right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(left.float_value + right.float_value);
          }
          throw invalid_argument("can\'t add numeric and non-numeric types");

        case ValueType::Bytes:
          if (right.type == ValueType::Bytes) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Bytes);
            }
            string new_value = *left.bytes_value + *right.bytes_value;
            return Variable(move(new_value));
          }
          throw invalid_argument("can\'t append bytes and non-bytes");

        case ValueType::Unicode:
          if (right.type == ValueType::Unicode) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Unicode);
            }
            wstring new_value = *left.unicode_value + *right.unicode_value;
            return Variable(move(new_value));
          }
          throw invalid_argument("can\'t append unicode and non-unicode");

        case ValueType::List:
        case ValueType::Tuple: {
          if (right.type != left.type) {
            throw invalid_argument("can\'t append list/tuple and non-list/tuple");
          }

          vector<shared_ptr<Variable>> result = *left.list_value;
          result.insert(result.end(), right.list_value->begin(), right.list_value->end());
          return Variable(result, (left.type == ValueType::Tuple));
        }

        default:
          throw invalid_argument("invalid type to Addition");
      }
      break;

    case BinaryOperator::Subtraction:
      // handle set-difference operation
      if ((left.type == ValueType::Set) && (right.type == ValueType::Set)) {
        unordered_set<Variable> result = *left.set_value;
        for (const auto& item : *right.set_value) {
          result.erase(item);
        }
        return Variable(move(result));
      }

      // else, it's the same as left + (-right); just do that
      return execute_binary_operator(BinaryOperator::Addition, left,
          execute_unary_operator(UnaryOperator::Negative, right));

    case BinaryOperator::Multiplication: {
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
          throw invalid_argument("list/tuple multipliers must be Int or Bool");
        }

        bool is_tuple = (list->type == ValueType::Tuple);

        // short-circuit cases first
        if (list->value_known && list->list_value->empty()) {
          return Variable(vector<shared_ptr<Variable>>(), is_tuple);
        }
        if (multiplier->value_known && (multiplier->int_value == 0)) {
          return Variable(vector<shared_ptr<Variable>>(), is_tuple);
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
        return Variable(move(result), is_tuple);
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Int);
            }
            return Variable(left.int_value * right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(left.int_value * right.float_value);
          }
          throw invalid_argument("can\'t add numeric and non-numeric types");

        case ValueType::Float:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(left.float_value * right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(left.float_value * right.float_value);
          }
          throw invalid_argument("can\'t add numeric and non-numeric types");

        default:
          throw invalid_argument("invalid type to Multiplication");
      }
      break;
    }

    case BinaryOperator::Division:
      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(static_cast<double>(left.int_value) /
                static_cast<double>(right.int_value));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(static_cast<double>(left.int_value) / right.float_value);
          }
          throw invalid_argument("can\'t divide numeric and non-numeric types");

        case ValueType::Float:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(left.float_value / static_cast<double>(right.int_value));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(left.float_value / right.float_value);
          }
          throw invalid_argument("can\'t divide numeric and non-numeric types");

        default:
          throw invalid_argument("invalid type to Division");
      }
      break;

    case BinaryOperator::Modulus:
      if (left.type == ValueType::Bytes) {
        // TODO
        throw invalid_argument("Bytes Modulus not yet implemented");
      }
      if (left.type == ValueType::Unicode) {
        // TODO
        throw invalid_argument("Unicode Modulus not yet implemented");
      }

      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Int);
            }
            return Variable(left.int_value % right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(fmod(static_cast<double>(left.int_value), right.float_value));
          }
          throw invalid_argument("can\'t modulus numeric and non-numeric types");

        case ValueType::Float:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(fmod(left.float_value, static_cast<double>(right.int_value)));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(fmod(left.float_value, right.float_value));
          }
          throw invalid_argument("can\'t modulus numeric and non-numeric types");

        default:
          throw invalid_argument("invalid type to Modulus");
      }
      break;

    case BinaryOperator::IntegerDivision:
      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Int);
            }
            return Variable(left.int_value / right.int_value);
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(floor(static_cast<double>(left.int_value) / right.float_value));
          }
          throw invalid_argument("can\'t integer divide numeric and non-numeric types");

        case ValueType::Float:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(floor(left.float_value / static_cast<double>(right.int_value)));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(floor(left.float_value / right.float_value));
          }
          throw invalid_argument("can\'t integer divide numeric and non-numeric types");

        default:
          throw invalid_argument("invalid type to IntegerDivision");
      }
      break;

    case BinaryOperator::Exponentiation:
      switch (left.type) {
        case ValueType::Bool:
        case ValueType::Int:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (right.value_known && (right.int_value == 0)) {
              return Variable(static_cast<int64_t>(0));
            }
            if (left.value_known && (left.int_value == 1)) {
              return Variable(static_cast<int64_t>(1));
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
              return Variable(pow(static_cast<double>(left.int_value), static_cast<double>(right.float_value)));
            }
            return Variable(ipow(left.int_value, right.int_value));
          }

          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(pow(static_cast<double>(left.int_value), right.float_value));
          }
          throw invalid_argument("can\'t exponentiate numeric and non-numeric types");

        case ValueType::Float:
          if ((right.type == ValueType::Bool) || (right.type == ValueType::Int)) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(pow(left.float_value, static_cast<double>(right.int_value)));
          }
          if (right.type == ValueType::Float) {
            if (!left.value_known || !right.value_known) {
              return Variable(ValueType::Float);
            }
            return Variable(pow(left.float_value, right.float_value));
          }
          throw invalid_argument("can\'t exponentiate numeric and non-numeric types");

        default:
          throw invalid_argument("invalid type to Exponentiation");
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
