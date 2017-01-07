#include <memory>
#include <stdio.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>

#include "env.hh"

using namespace std;



PyValue::PyValue() : value_type(PyValueUnbound) { }
PyValue::PyValue(bool v) : value_type(PyValueBoolean), value_bool(v) { }
PyValue::PyValue(long v) : value_type(PyValueInteger), value_int(v) { }
PyValue::PyValue(double v) : value_type(PyValueFloat), value_float(v) { }
PyValue::PyValue(const string& v) : value_type(PyValueString), value_string(new string(v)) { }
PyValue::PyValue(const vector<PyValue>& v, bool is_tuple = false) : value_type(is_tuple ? PyValueTuple : PyValueList), value_list(new vector<PyValue>(v)) { }
PyValue::PyValue(const unordered_set<PyValue>& v) : value_type(PyValueSet), value_set(new unordered_set<PyValue>(v)) { }
PyValue::PyValue(const unordered_map<PyValue, PyValue>& v, bool is_object = false) : value_type(is_object ? PyValueObject : PyValueDict), value_dict(new unordered_map<PyValue, PyValue>(v)) { }
PyValue::PyValue(FunctionDefinition* v) : value_function(v) { }
PyValue::PyValue(LambdaDefinition* v) : value_lambda(v) { }
PyValue::PyValue(ClassDefinition* v) : value_class(v) { }

PyValue::~PyValue() {
  switch (value_type) {
    case PyValueString:
      delete value_string;
      break;
    case PyValueList:
    case PyValueTuple:
      delete value_list;
      break;
    case PyValueSet:
      delete value_set;
      break;
    case PyValueDict:
    case PyValueObject:
      delete value_dict;
      break;
    default:
      break;
  }
}

void PyValue::set_none() {
  value_type = PyValueNone;
}



PyValue PyValue::__in__(const PyValue& other) const {
  return other.__contains__(*this);
}

PyValue PyValue::__not__(const PyValue& other) const {
  if (!valid())
    throw domain_error("can't __not__ an unbound value");
  return PyValue(!is_truthy());
}

PyValue PyValue::__eq__(const PyValue& other) const {
  switch (value_type) {
    case PyValueNone:
      return (other.value_type == None);

    case PyValueBoolean:
    case PyValueInteger:
    case PyValueFloat: {
      PyValue this_cast = numeric_cast();
      other = other.numeric_cast();

      if (this_cast.value_type == PyValueInteger && other.value_type == PyValueInteger)
        return this_cast.value_int == other.value_int;
      if (this_cast.value_type == PyValueFloat && other.value_type == PyValueInteger)
        return this_cast.value_float == other.value_int;
      if (this_cast.value_type == PyValueInteger && other.value_type == PyValueFloat)
        return this_cast.value_int == other.value_float;
      if (this_cast.value_type == PyValueFloat && other.value_type == PyValueFloat)
        return this_cast.value_float == other.value_float;

      throw domain_error("numeric equality comparison given wrong types");
    }

    case PyValueString:
      if (other.value_type != PyValueString)
        return false;
      return !value_string.compare(other.value_string);

    case PyValueList:
    case PyValueTuple:
      if (other.value_type != value_type)
        return false;
      if (other.value_list.size() != value_list.size())
        return false;
      for (size_t x = 0; x < value_list.size(); x++) {
        if (other.value_list[x] != value_list[x])
          return false;
      }
      return true;

    case PyValueSet:
      if (other.value_type != value_type)
        return false;
      if (other.value_set.size() != value_set.size())
        return false;
      if (other.value_set != value_set)
        return false;
      return true;

    case PyValueDict:
    case PyValueObject:
      if (other.value_type != value_type)
        return false;
      if (other.value_dict.size() != value_dict.size())
        return false;
      for (const auto& it : value_dict) {
        auto other_it = other.value_dict.find(it.first);
        if (other_it == other.value_dict.end())
          return false;
        if (other_it->second != it.second)
          return false;
      }
      return true;

    case PyValueFunction:
      return (other.value_type == PyValueFunction && other.value_function == value_function);

    case PyValueLambda:
      return (other.value_type == PyValueLambda && other.value_lambda == value_lambda);

    case PyValueClass:
      return (other.value_type == PyValueClass && other.value_class == value_class);
  }
}

PyValue PyValue::__ne__(const PyValue& other) const {
  return __eq__(other);
}

PyValue PyValue::__lt__(const PyValue& other) const {
  // if either side is None then it's false
  if (left.value_type == PyValueNone || right.value_type == PyValueNone)
    return false;

  if (left.value_type == PyValueString && right.value_type == PyValueString)
    return value_string.compare(other.value_string) < 0;

  // TODO other bullshit like objects, lists, etc.

  left = left.numeric_cast();
  right = right.numeric_cast();
  if (!left.valid() || !right.valid())
    throw domain_error("__lt__: one or both sides are invalid");

  if (left.value_type == PyValueInteger && right.value_type == PyValueInteger)
    return PyValue(left.value_int < right.value_int);
  if (left.value_type == PyValueFloat && right.value_type == PyValueInteger)
    return PyValue(left.value_float < right.value_int);
  if (left.value_type == PyValueInteger && right.value_type == PyValueFloat)
    return PyValue(left.value_int < right.value_float);
  if (left.value_type == PyValueFloat && right.value_type == PyValueFloat)
    return PyValue(left.value_float < right.value_float);

  throw domain_error("__lt__: bad type given");
}

PyValue PyValue::__le__(const PyValue& other) const {
  // if both sides are None then it's true
  if (left.value_type == PyValueNone && right.value_type == PyValueNone)
    return true;
  // but of only one side is None then it's false
  if (left.value_type == PyValueNone || right.value_type == PyValueNone)
    return false;

  if (left.value_type == PyValueString && right.value_type == PyValueString)
    return value_string.compare(other.value_string) <= 0;

  // TODO other bullshit like objects, lists, etc.

  left = left.numeric_cast();
  right = right.numeric_cast();
  if (!left.valid() || !right.valid())
    throw domain_error("__le__: one or both sides are invalid");

  if (left.value_type == PyValueInteger && right.value_type == PyValueInteger)
    return PyValue(left.value_int <= right.value_int);
  if (left.value_type == PyValueFloat && right.value_type == PyValueInteger)
    return PyValue(left.value_float <= right.value_int);
  if (left.value_type == PyValueInteger && right.value_type == PyValueFloat)
    return PyValue(left.value_int <= right.value_float);
  if (left.value_type == PyValueFloat && right.value_type == PyValueFloat)
    return PyValue(left.value_float <= right.value_float);

  throw domain_error("__le__: bad type given");
}

PyValue PyValue::__gt__(const PyValue& other) const {
  return !__le__(other);
}

PyValue PyValue::__ge__(const PyValue& other) const {
  return !__lt__(other);
}



bool PyValue::valid() const {
  return value_type != PyValueUnbound;
}

bool PyValue::is_truthy() const {
  if (value_type == PyValueNone)
    return PyValue(false);
  if (value_type == PyValueBoolean)
    return PyValue(value_bool);
  if (value_type == PyValueInteger)
    return PyValue(value_int != 0);
  if (value_type == PyValueFloat)
    return PyValue(value_float != 0.0);
  if (value_type == PyValueString)
    return PyValue(!value_string->empty());
  if (value_type == PyValueList)
    return PyValue(!value_list->empty());
  if (value_type == PyValueTuple)
    return PyValue(!value_list->empty());
  if (value_type == PyValueSet)
    return PyValue(!value_set->empty());
  if (value_type == PyValueDict)
    return PyValue(!value_dict->empty());
  return PyValue(true); // object, function, lambda, class
}

PyValue numeric_cast() const {
  if (value_type == PyValueBoolean)
    return PyValue(value_bool ? 1 : 0);
  if (value_type == PyValueInteger)
    return PyValue(value_int);
  if (value_type == PyValueFloat)
    return PyValue(value_float);

  throw domain_error("numeric_cast: can't convert to number");
}

string PyValue::str() const {
  switch (value_type) {
    case PyValueUnbound:
      return "__PyValueUnbound__";
    case PyValueNone:
      return "None";
    case PyValueBoolean:
      return value_bool ? "True" : "False";
    case PyValueInteger: {
      char buffer[0x40];
      sprintf(buffer, "%ld", value_int);
      return buffer;
    }
    case PyValueFloat: {
      char buffer[0x40];
      sprintf(buffer, "%g", value_float);
      return buffer;
    }
    case PyValueString:
      return "\'" + *value_string + "\'";
    case PyValueList: {
      string s = "[";
      for (int x = 0; x < value_set->size(); x++) {
        if (x > 0)
          s += ", ";
        s += (*value_set)[x].str();
      }
      return s + "]";
    }
    case PyValueDict: {
      string s = "{";
      for (auto it = value_dict->begin(); it != value_dict->end(); it++) {
        if (it != value_dict->begin())
          s += ", ";
        s += it->first.str() + ": " + it->second.str();
      }
      return s + "}";
    }
    case PyValueObject: {
      string s = "__PyValueObject__(";
      for (auto it = value_dict->begin(); it != value_dict->end(); it++) {
        if (it != value_dict->begin())
          s += ", ";
        s += it->first.str() + "=" + it->second.str();
      }
      return s + ")";
    }
  }
}



PyValue PyExecUnaryOperator(UnaryOperator oper, const PyValue& value) {

  if (value.value_type == PyValueUnbound)
    throw domain_error("nubound argument to unary operator");

  switch (oper) {
    case LogicalNotOperator:
      return PyValue(!value.is_truthy());

    case NotOperator: // bitwise
      return ~value;

    case PositiveOperator:
      return +value;

    case NegativeOperator:
      return -value;

    case RepresentationOperator:
      return PyValue(value->str());

    case YieldOperator:
      throw domain_error("can't statically evaluate yield");
  }
}

PyValue PyExecBinaryOperator(BinaryOperator oper, const PyValue& left, const PyValue& right) {
  if (left.value_type == PyValueUnbound && right.value_type == PyValueUnbound)
    throw domain_error("two unbound arguments to binary operator");

  switch (oper) {
    case LogicalOrOperator:
      // you might think this would be valid if left is unbound but right is
      // falsey, but it's not - you can't reduce out an unbound left value
      // because it could contain function calls that have side effects
      if (!left.valid())
        throw domain_error("left not valid for logical or");
      if (left.is_truthy())
        return PyValue(left);
      return PyValue(right);

    case LogicalAndOperator:
      // like above, you might think this is valid if left is unbound but right
      // is always truthy, but it's not
      if (!left.valid())
        throw domain_error("left not valid for logical and");
      if (!left.is_truthy())
        return PyValue(left);
      return PyValue(right);


    // TODO somehow reduce code cuplication here with the comparison operators

    case LessThanOperator:
      return left.__lt__(right);

    case GreaterThanOperator:
      return left.__gt__(right);

    case LessOrEqualOperator:
      return left.__le__(right);

    case GreaterOrEqualOperator:
      return left.__ge__(right);

    case EqualityOperator:
      return left.__eq__(right);

    case NotEqualOperator:
      return left.__ne__(right);

    case InOperator:
      return left.__in__(right);

    case NotInOperator:

    case IsOperator:
    case IsNotOperator:

    case OrOperator:
    case AndOperator:
    case XorOperator:
    case LeftShiftOperator:
    case RightShiftOperator:

    case AdditionOperator:
    case SubtractionOperator:
    case MultiplicationOperator:
    case DivisionOperator:
    case ModulusOperator:
    case IntegerDivisionOperator:
    case ExponentiationOperator:
  }
}

PyValue PyExecTernaryOperator(TernaryOperator oper, const PyValue& left, const PyValue& center, const PyValue& right);
