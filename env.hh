#ifndef _ENV_HH
#define _ENV_HH

#include <map>
#include <string>

using namespace std;

enum PyValueType {
  PyValueUnbound = 0,
  PyValueNone = 1,
  PyValueBoolean = 2,
  PyValueInteger = 3,
  PyValueFloat = 4,
  PyValueString = 5,
  PyValueList = 6,
  PyValueDict = 7,
  PyValueObject = 8,
};

struct PyValue {
  PyValue();
  ~PyValue();
  string str() const;

  PyValueType value_type;
  union {
    bool value_bool;
    long value_int;
    double value_float;
    string* value_string;
    vector<PyValue>* value_list;
    map<PyValue, PyValue>* value_dict; // also used for object attrs
  };
};

struct GlobalEnvironment; // gee I seem to be using a lot of forward declarations these days

struct LocalEnvironment {
  map<string, PyValue> locals;

  LocalEnvironment* parent_env; // may be NULL, for instance if this is the top level of a function
  LocalEnvironment* module_env;
  GlobalEnvironment* global;
};

struct GlobalEnvironment {
  map<string, LocalEnvironment> modules;
};

#endif // _ENV_HH
