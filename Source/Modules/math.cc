#include "math.hh"

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include "../Compiler/Contexts.hh"
#include "../Compiler/BuiltinFunctions.hh"
#include "../Types/Tuple.hh"

using namespace std;



extern shared_ptr<GlobalContext> global;

const double E = 2.718281828459045235360287471352;
const double PI = 3.141592653589793238462643383279;

static wstring __doc__ = L"Standard mathematical functions.";

static map<string, Value> globals({
  {"__doc__",     Value(ValueType::Unicode, __doc__)},
  {"__name__",    Value(ValueType::Unicode, L"math")},
  {"__package__", Value(ValueType::Unicode, L"")},

  {"e",           Value(ValueType::Float, E)},
  {"pi",          Value(ValueType::Float, PI)},
  {"tau",         Value(ValueType::Float, 2 * PI)},
  {"inf",         Value(ValueType::Float, INFINITY)},
  {"nan",         Value(ValueType::Float, NAN)},
});

shared_ptr<ModuleContext> math_initialize(GlobalContext* global_context) {
  Value None(ValueType::None);
  Value Bool(ValueType::Bool);
  Value Int(ValueType::Int);
  Value Float(ValueType::Float);
  Value Float_E(ValueType::Float, E);
  Value Tuple_Float_Int(ValueType::Tuple, vector<Value>({Float, Int}));
  Value Tuple_Float_Float(ValueType::Tuple, vector<Value>({Float, Float}));

  // note: we can't just use pointers directly to the appropriate math.h
  // functions because many of them are overloaded or macros

  // this is called by a few log functions
  static auto log_float = +[](double x, double b) -> double {
    if (b == E) {
      return log(x);
    }
    return log(x) / log(b);
  };

  vector<BuiltinFunctionDefinition> module_function_defs({
    // TODO: implement these:
    // fsum (I'm lazy)
    // isclose (lazy; has keyword-only args)

    // inspection

    {"isfinite", {Float}, Bool, void_fn_ptr([](double a) -> bool {
      return isfinite(a);
    }), false},

    {"isinf", {Float}, Bool, void_fn_ptr([](double a) -> bool {
      return isinf(a);
    }), false},

    {"isnan", {Float}, Bool, void_fn_ptr([](double a) -> bool {
      return isnan(a);
    }), false},

    // algorithms

    {"factorial", {Int}, Int, void_fn_ptr([](int64_t a, ExceptionBlock* exc_block) -> int64_t {
      if (a < 0) {
        raise_python_exception_with_message(exc_block, global->ValueError_class_id,
            "factorial input must be nonnegative");
      }
      int64_t ret = 1;
      for (; a > 1; a--) {
        ret *= a;
      }
      return ret;
    }), true},

    {"gcd", {Int, Int}, Int, void_fn_ptr([](int64_t a, int64_t b) -> int64_t {
      if (a < 0) {
        a = -a;
      }
      if (b < 0) {
        b = -b;
      }
      while (b) {
        int64_t t = b;
        b = a % b;
        a = t;
      }
      return a;
    }), false},

    // basic numerics

    {"ceil", {Float}, Int, void_fn_ptr([](double x) -> int64_t {
      return static_cast<int64_t>(ceil(x));
    }), false},

    {"floor", {Float}, Int, void_fn_ptr([](double x) -> int64_t {
      return static_cast<int64_t>(floor(x));
    }), false},

    {"trunc", {Float}, Int, void_fn_ptr([](double x) -> int64_t {
      return static_cast<int64_t>(trunc(x));
    }), false},

    {"copysign", {Float, Float}, Bool, void_fn_ptr([](double a, double b) -> double {
      return copysign(a, b);
    }), false},

    {"fabs", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return fabs(a);
    })}}, false},

    {"fmod", {{{Float, Float}, Float, void_fn_ptr([](double a, double b) -> double {
      return fmod(a, b);
    })}, {{Float, Int}, Float, void_fn_ptr([](double a, int64_t b) -> double {
      return fmod(a, b);
    })}, {{Int, Float}, Float, void_fn_ptr([](int64_t a, double b) -> double {
      return fmod(a, b);
    })}}, false},

    {"frexp", {Float}, Tuple_Float_Int, void_fn_ptr([](double a, ExceptionBlock* exc_block) -> void* {
      int e;
      double m = frexp(a, &e);

      int64_t m64 = *reinterpret_cast<const int64_t*>(&m);
      int64_t e64 = e;

      TupleObject* t = tuple_new(2, exc_block);
      tuple_set_item(t, 0, reinterpret_cast<void*>(m64), false, exc_block);
      tuple_set_item(t, 1, reinterpret_cast<void*>(e64), false, exc_block);
      return t;
    }), true},

    {"modf", {Float}, Tuple_Float_Float, void_fn_ptr([](double a, ExceptionBlock* exc_block) -> void* {
      double integral;
      double fractional = modf(a, &integral);

      int64_t fractional64 = *reinterpret_cast<const int64_t*>(&fractional);
      int64_t integral64 = *reinterpret_cast<const int64_t*>(&integral);

      TupleObject* t = tuple_new(2, exc_block);
      tuple_set_item(t, 0, reinterpret_cast<void*>(fractional64), false, exc_block);
      tuple_set_item(t, 1, reinterpret_cast<void*>(integral64), false, exc_block);
      return t;
    }), true},

    // exponents

    {"exp", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return exp(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return exp(a);
    })}}, false},

    {"expm1", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return expm1(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return expm1(a);
    })}}, false},

    {"ldexp", {{{Float, Int}, Float, void_fn_ptr([](double a, int64_t b) -> double {
      return ldexp(a, b);
    })}, {{Int, Int}, Float, void_fn_ptr([](int64_t a, int64_t b) -> double {
      return ldexp(a, b);
    })}}, false},

    {"pow", {{{Float, Float}, Float, void_fn_ptr([](double a, double b) -> double {
      return pow(a, b);
    })}, {{Float, Int}, Float, void_fn_ptr([](double a, int64_t b) -> double {
      return pow(a, b);
    })}, {{Int, Float}, Float, void_fn_ptr([](int64_t a, double b) -> double {
      return pow(a, b);
    })}, {{Int, Int}, Float, void_fn_ptr([](int64_t a, int64_t b) -> double {
      return pow(a, b);
    })}}, false},

    {"hypot", {{{Float, Float}, Float, void_fn_ptr([](double a, double b) -> double {
      return hypot(a, b);
    })}, {{Float, Int}, Float, void_fn_ptr([](double a, int64_t b) -> double {
      return hypot(a, b);
    })}, {{Int, Float}, Float, void_fn_ptr([](int64_t a, double b) -> double {
      return hypot(a, b);
    })}, {{Int, Int}, Float, void_fn_ptr([](int64_t a, int64_t b) -> double {
      return hypot(a, b);
    })}}, false},

    {"sqrt", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return sqrt(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return sqrt(a);
    })}}, false},

    // logarithms

    {"log", {{{Float, Float_E}, Float, void_fn_ptr(log_float)},
    {{Float, Int}, Float, void_fn_ptr([](double a, int64_t b) -> double {
      return log_float(a, b);
    })}, {{Int, Float_E}, Float, void_fn_ptr([](int64_t a, double b) -> double {
      return log_float(a, b);
    })}, {{Int, Int}, Float, void_fn_ptr([](int64_t a, int64_t b) -> double {
      return log_float(a, b);
    })}}, false},

    {"log1p", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return log1p(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return log1p(a);
    })}}, false},

    {"log2", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return log2(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return log2(a);
    })}}, false},

    {"log10", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return log10(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return log10(a);
    })}}, false},

    // trigonometry
    {"sin", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return sin(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return sin(a);
    })}}, false},

    {"cos", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return cos(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return cos(a);
    })}}, false},

    {"tan", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return tan(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return tan(a);
    })}}, false},

    {"asin", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return asin(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return asin(a);
    })}}, false},

    {"acos", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return acos(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return acos(a);
    })}}, false},

    {"atan", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return atan(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return atan(a);
    })}}, false},

    {"atan2", {{{Float, Float}, Float, void_fn_ptr([](double a, double b) -> double {
      return atan2(a, b);
    })}, {{Float, Int}, Float, void_fn_ptr([](double a, int64_t b) -> double {
      return atan2(a, b);
    })}, {{Int, Float}, Float, void_fn_ptr([](int64_t a, double b) -> double {
      return atan2(a, b);
    })}, {{Int, Int}, Float, void_fn_ptr([](int64_t a, int64_t b) -> double {
      return atan2(a, b);
    })}}, false},

    // hyperbolic functions
    {"sinh", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return sinh(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return sinh(a);
    })}}, false},

    {"cosh", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return cosh(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return cosh(a);
    })}}, false},

    {"tanh", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return tanh(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return tanh(a);
    })}}, false},

    {"asinh", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return asinh(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return asinh(a);
    })}}, false},

    {"acosh", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return acosh(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return acosh(a);
    })}}, false},

    {"atanh", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return atanh(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return atanh(a);
    })}}, false},

    // angles

    {"degrees", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return a * (180.0 / PI);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return a * (180.0 / PI);
    })}}, false},

    {"radians", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return a * (PI / 180.0);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return a * (PI / 180.0);
    })}}, false},

    // statistics

    {"erf", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return erf(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return erf(a);
    })}}, false},

    {"erfc", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return erfc(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return erfc(a);
    })}}, false},

    // gamma

    {"gamma", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return tgamma(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return tgamma(a);
    })}}, false},

    {"lgamma", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return lgamma(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return lgamma(a);
    })}}, false},
  });

  shared_ptr<ModuleContext> module(new ModuleContext(global_context, "math", globals));
  for (auto& def : module_function_defs) {
    module->create_builtin_function(def);
  }
  return module;
}
