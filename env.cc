#include <tr1/memory>
#include <stdio.h>

#include <string>
#include <vector>

#include "env.hh"

using namespace std;
using namespace std::tr1;

PyValue::PyValue() : value_type(PyValueUnbound) { }

PyValue::~PyValue() {
  switch (value_type) {
    case PyValueString:
      delete value_string;
      break;
    case PyValueList:
      delete value_list;
      break;
    case PyValueDict:
    case PyValueObject:
      delete value_dict;
      break;
    default:
      break;
  }
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
      for (int x = 0; x < value_list->size(); x++) {
        if (x > 0)
          s += ", ";
        s += (*value_list)[x].str();
      }
      return s + "]";
    }
    case PyValueDict: {
      string s = "{";
      for (map<PyValue, PyValue>::iterator it = value_dict->begin(); it != value_dict->end(); it++) {
        if (it != value_dict->begin())
          s += ", ";
        s += it->first.str() + ": " + it->second.str();
      }
      return s + "}";
    }
    case PyValueObject: {
      string s = "__PyValueObject__(";
      for (map<PyValue, PyValue>::iterator it = value_dict->begin(); it != value_dict->end(); it++) {
        if (it != value_dict->begin())
          s += ", ";
        s += it->first.str() + "=" + it->second.str();
      }
      return s + ")";
    }
  }
}

