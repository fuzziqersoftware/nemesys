#include "math.hh"

#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include "../Analysis.hh"
#include "../BuiltinFunctions.hh"

using namespace std;



const double E = 2.718281828459045235360287471352;
const double PI = 3.141592653589793238462643383279;

extern shared_ptr<GlobalAnalysis> global;

static wstring __doc__ = L"Standard mathematical functions.";

static map<string, Variable> globals({
  {"__doc__",     Variable(ValueType::Unicode, __doc__)},
  {"__package__", Variable(ValueType::Unicode, L"")},

  {"e",           Variable(ValueType::Float, E)},
  {"pi",          Variable(ValueType::Float, PI)},
  {"tau",         Variable(ValueType::Float, 2 * PI)},
  {"inf",         Variable(ValueType::Float, INFINITY)},
  {"nan",         Variable(ValueType::Float, NAN)},
});

std::shared_ptr<ModuleAnalysis> math_module(new ModuleAnalysis("math", globals));

void math_initialize() {
  Variable None(ValueType::None);
  Variable Bool(ValueType::Bool);
  Variable Int(ValueType::Int);
  Variable Float(ValueType::Float);
  Variable Float_E(ValueType::Float, E);

  // note: we can't just use pointers directly to the appropriate math.h
  // functions because many of them are overloaded or macros

  // TODO: many of these functions should have fragments that take Ints also

  vector<BuiltinFunctionDefinition> module_function_defs({
    // unimplemented:
    //fsum (I'm lazy)
    //isclose (lazy; has keyword-only args)
    //frexp (returns a tuple)
    //modf (returns a tuple)

    // inspection
    {"isfinite", {Float}, Bool, void_fn_ptr([](double a) -> bool {
      return isfinite(a);
    }), false, false},
    {"isinf", {Float}, Bool, void_fn_ptr([](double a) -> bool {
      return isinf(a);
    }), false, false},
    {"isnan", {Float}, Bool, void_fn_ptr([](double a) -> bool {
      return isnan(a);
    }), false, false},

    // algorithms
    {"factorial", {Int}, Int, void_fn_ptr([](int64_t a, ExceptionBlock* exc_block) -> int64_t {
      if (a < 0) {
        raise_python_exception(exc_block, create_instance(ValueError_class_id));
      }
      int64_t ret = 1;
      for (; a > 1; a--) {
        ret *= a;
      }
      return ret;
    }), true, false},
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
    }), false, false},

    // basic numerics
    {"ceil", {Float}, Int, void_fn_ptr([](double x) -> int64_t {
      return static_cast<int64_t>(ceil(x));
    }), false, false},
    {"floor", {Float}, Int, void_fn_ptr([](double x) -> int64_t {
      return static_cast<int64_t>(floor(x));
    }), false, false},
    {"trunc", {Float}, Int, void_fn_ptr([](double x) -> int64_t {
      return static_cast<int64_t>(trunc(x));
    }), false, false},
    {"copysign", {Float, Float}, Bool, void_fn_ptr([](double a, double b) -> double {
      return copysign(a, b);
    }), false, false},
    {"fabs", {Float}, Float, void_fn_ptr([](double a) -> double {
      return fabs(a);
    }), false, false},
    {"fmod", {Float, Float}, Float, void_fn_ptr([](double a, double b) -> double {
      return fmod(a, b);
    }), false, false},

    // exponents
    {"exp", {Float}, Float, void_fn_ptr([](double a) -> double {
      return exp(a);
    }), false, false},
    {"expm1", {Float}, Float, void_fn_ptr([](double a) -> double {
      return expm1(a);
    }), false, false},
    {"ldexp", {Float, Int}, Float, void_fn_ptr([](double a, int64_t b) -> double {
      return ldexp(a, b);
    }), false, false},
    {"pow", {Float, Float}, Float, void_fn_ptr([](double a, double b) -> double {
      return pow(a, b);
    }), false, false},

    {"hypot", {Float, Float}, Float, void_fn_ptr([](double a, double b) -> double {
      return hypot(a, b);
    }), false, false},
    {"sqrt", {{{Float}, Float, void_fn_ptr([](double a) -> double {
      return sqrt(a);
    })}, {{Int}, Float, void_fn_ptr([](int64_t a) -> double {
      return sqrt(a);
    })}}, false, false},

    // logarithms
    {"log", {Float, Float_E}, Float, void_fn_ptr([](double x, double b) -> double {
      if (b == E) {
        return log(x);
      }
      return log(x) / log(b);
    }), false, false},
    {"log1p", {Float}, Float, void_fn_ptr([](double a) -> double {
      return log1p(a);
    }), false, false},
    {"log2", {Float}, Float, void_fn_ptr([](double a) -> double {
      return log2(a);
    }), false, false},
    {"log10", {Float}, Float, void_fn_ptr([](double a) -> double {
      return log10(a);
    }), false, false},

    // trigonometry
    {"sin", {Float}, Float, void_fn_ptr([](double a) -> double {
      return sin(a);
    }), false, false},
    {"cos", {Float}, Float, void_fn_ptr([](double a) -> double {
      return cos(a);
    }), false, false},
    {"tan", {Float}, Float, void_fn_ptr([](double a) -> double {
      return tan(a);
    }), false, false},
    {"asin", {Float}, Float, void_fn_ptr([](double a) -> double {
      return asin(a);
    }), false, false},
    {"acos", {Float}, Float, void_fn_ptr([](double a) -> double {
      return acos(a);
    }), false, false},
    {"atan", {Float}, Float, void_fn_ptr([](double a) -> double {
      return atan(a);
    }), false, false},
    {"atan2", {Float, Float}, Float, void_fn_ptr([](double a, double b) -> double {
      return atan2(a, b);
    }), false, false},

    // hyperbolic functions
    {"sinh", {Float}, Float, void_fn_ptr([](double a) -> double {
      return sinh(a);
    }), false, false},
    {"cosh", {Float}, Float, void_fn_ptr([](double a) -> double {
      return cosh(a);
    }), false, false},
    {"tanh", {Float}, Float, void_fn_ptr([](double a) -> double {
      return tanh(a);
    }), false, false},
    {"asinh", {Float}, Float, void_fn_ptr([](double a) -> double {
      return asinh(a);
    }), false, false},
    {"acosh", {Float}, Float, void_fn_ptr([](double a) -> double {
      return acosh(a);
    }), false, false},
    {"atanh", {Float}, Float, void_fn_ptr([](double a) -> double {
      return atanh(a);
    }), false, false},

    // angles
    {"degrees", {Float}, Float, void_fn_ptr([](double a) -> double {
      return a * (180.0 / PI);
    }), false, false},
    {"radians", {Float}, Float, void_fn_ptr([](double a) -> double {
      return a * (PI / 180.0);
    }), false, false},

    // statistics
    {"erf", {Float}, Float, void_fn_ptr([](double a) -> double {
      return erf(a);
    }), false, false},
    {"erfc", {Float}, Float, void_fn_ptr([](double a) -> double {
      return erfc(a);
    }), false, false},

    // gamma
    {"gamma", {Float}, Float, void_fn_ptr([](double a) -> double {
      return tgamma(a);
    }), false, false},
    {"lgamma", {Float}, Float, void_fn_ptr([](double a) -> double {
      return lgamma(a);
    }), false, false},
  });

  for (auto& def : module_function_defs) {
    math_module->create_builtin_function(def);
  }
}
