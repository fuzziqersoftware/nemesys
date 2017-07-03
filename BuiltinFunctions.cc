#include "BuiltinFunctions.hh"

#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Analysis.hh"
#include "BuiltinTypes.hh"

using namespace std;



static void builtin_print(UnicodeObject* str) {
  fprintf(stdout, "%.*ls\n", static_cast<int>(str->count), str->data);
}



const unordered_map<string, int64_t> builtin_function_to_id({
  {"print", -1},
});



unordered_map<int64_t, FunctionContext> builtin_function_definitions({
  {builtin_function_to_id.at("print"),
    FunctionContext(NULL, builtin_function_to_id.at("print"), "print",
        {Variable(ValueType::Unicode)}, Variable(ValueType::None),
        reinterpret_cast<const void*>(&builtin_print))},
});



unordered_map<Variable, shared_ptr<Variable>> empty_dict_contents;
static const Variable empty_dict(empty_dict_contents);



// TODO: we'll have to do more than this in the future for builtins
const unordered_map<string, Variable> builtin_names({
  {"__annotations__",           empty_dict},
  {"__build_class__",           Variable(ValueType::Function)},
  {"__debug__",                 Variable(true)}, // in CPython, False iff -O was given
  {"__doc__",                   Variable(ValueType::None)},
  {"__import__",                Variable(ValueType::Function)},
  {"__loader__",                Variable(ValueType::None)},
  {"__name__",                  Variable(ValueType::Unicode)},
  {"__package__",               Variable(ValueType::None)},
  {"__spec__",                  Variable(ValueType::None)},
  {"ArithmeticError",           Variable(ValueType::Function)},
  {"AssertionError",            Variable(ValueType::Function)},
  {"AttributeError",            Variable(ValueType::Function)},
  {"BaseException",             Variable(ValueType::Function)},
  {"BlockingIOError",           Variable(ValueType::Function)},
  {"BrokenPipeError",           Variable(ValueType::Function)},
  {"BufferError",               Variable(ValueType::Function)},
  {"BytesWarning",              Variable(ValueType::Function)},
  {"ChildProcessError",         Variable(ValueType::Function)},
  {"ConnectionAbortedError",    Variable(ValueType::Function)},
  {"ConnectionError",           Variable(ValueType::Function)},
  {"ConnectionRefusedError",    Variable(ValueType::Function)},
  {"ConnectionResetError",      Variable(ValueType::Function)},
  {"DeprecationWarning",        Variable(ValueType::Function)},
  {"EOFError",                  Variable(ValueType::Function)},
  {"Ellipsis",                  Variable()},
  {"EnvironmentError",          Variable(ValueType::Function)},
  {"Exception",                 Variable(ValueType::Function)},
  {"FileExistsError",           Variable(ValueType::Function)},
  {"FileNotFoundError",         Variable(ValueType::Function)},
  {"FloatingPointError",        Variable(ValueType::Function)},
  {"FutureWarning",             Variable(ValueType::Function)},
  {"GeneratorExit",             Variable(ValueType::Function)},
  {"IOError",                   Variable(ValueType::Function)},
  {"ImportError",               Variable(ValueType::Function)},
  {"ImportWarning",             Variable(ValueType::Function)},
  {"IndentationError",          Variable(ValueType::Function)},
  {"IndexError",                Variable(ValueType::Function)},
  {"InterruptedError",          Variable(ValueType::Function)},
  {"IsADirectoryError",         Variable(ValueType::Function)},
  {"KeyError",                  Variable(ValueType::Function)},
  {"KeyboardInterrupt",         Variable(ValueType::Function)},
  {"LookupError",               Variable(ValueType::Function)},
  {"MemoryError",               Variable(ValueType::Function)},
  {"ModuleNotFoundError",       Variable(ValueType::Function)},
  {"NameError",                 Variable(ValueType::Function)},
  {"NotADirectoryError",        Variable(ValueType::Function)},
  {"NotImplemented",            Variable()},
  {"NotImplementedError",       Variable(ValueType::Function)},
  {"OSError",                   Variable(ValueType::Function)},
  {"OverflowError",             Variable(ValueType::Function)},
  {"PendingDeprecationWarning", Variable(ValueType::Function)},
  {"PermissionError",           Variable(ValueType::Function)},
  {"ProcessLookupError",        Variable(ValueType::Function)},
  {"RecursionError",            Variable(ValueType::Function)},
  {"ReferenceError",            Variable(ValueType::Function)},
  {"ResourceWarning",           Variable(ValueType::Function)},
  {"RuntimeError",              Variable(ValueType::Function)},
  {"RuntimeWarning",            Variable(ValueType::Function)},
  {"StopAsyncIteration",        Variable(ValueType::Function)},
  {"StopIteration",             Variable(ValueType::Function)},
  {"SyntaxError",               Variable(ValueType::Function)},
  {"SyntaxWarning",             Variable(ValueType::Function)},
  {"SystemError",               Variable(ValueType::Function)},
  {"SystemExit",                Variable(ValueType::Function)},
  {"TabError",                  Variable(ValueType::Function)},
  {"TimeoutError",              Variable(ValueType::Function)},
  {"TypeError",                 Variable(ValueType::Function)},
  {"UnboundLocalError",         Variable(ValueType::Function)},
  {"UnicodeEncodeError",        Variable(ValueType::Function)},
  {"UnicodeTranslateError",     Variable(ValueType::Function)},
  {"UserWarning",               Variable(ValueType::Function)},
  {"UnicodeDecodeError",        Variable(ValueType::Function)},
  {"UnicodeError",              Variable(ValueType::Function)},
  {"UnicodeWarning",            Variable(ValueType::Function)},
  {"ValueError",                Variable(ValueType::Function)},
  {"Warning",                   Variable(ValueType::Function)},
  {"ZeroDivisionError",         Variable(ValueType::Function)},
  {"abs",                       Variable(ValueType::Function)},
  {"all",                       Variable(ValueType::Function)},
  {"any",                       Variable(ValueType::Function)},
  {"ascii",                     Variable(ValueType::Function)},
  {"bin",                       Variable(ValueType::Function)},
  {"bool",                      Variable(ValueType::Function)},
  {"bytearray",                 Variable(ValueType::Function)},
  {"bytes",                     Variable(ValueType::Function)},
  {"callable",                  Variable(ValueType::Function)},
  {"chr",                       Variable(ValueType::Function)},
  {"classmethod",               Variable(ValueType::Function)},
  {"compile",                   Variable(ValueType::Function)},
  {"complex",                   Variable(ValueType::Function)},
  {"copyright",                 Variable(ValueType::Function)},
  {"credits",                   Variable(ValueType::Function)},
  {"delattr",                   Variable(ValueType::Function)},
  {"dict",                      Variable(ValueType::Function)},
  {"dir",                       Variable(ValueType::Function)},
  {"divmod",                    Variable(ValueType::Function)},
  {"enumerate",                 Variable(ValueType::Function)},
  {"eval",                      Variable(ValueType::Function)},
  {"exec",                      Variable(ValueType::Function)},
  {"exit",                      Variable(ValueType::Function)},
  {"filter",                    Variable(ValueType::Function)},
  {"float",                     Variable(ValueType::Function)},
  {"format",                    Variable(ValueType::Function)},
  {"frozenset",                 Variable(ValueType::Function)},
  {"getattr",                   Variable(ValueType::Function)},
  {"globals",                   Variable(ValueType::Function)},
  {"hasattr",                   Variable(ValueType::Function)},
  {"hash",                      Variable(ValueType::Function)},
  {"help",                      Variable(ValueType::Function)},
  {"hex",                       Variable(ValueType::Function)},
  {"id",                        Variable(ValueType::Function)},
  {"input",                     Variable(ValueType::Function)},
  {"int",                       Variable(ValueType::Function)},
  {"isinstance",                Variable(ValueType::Function)},
  {"issubclass",                Variable(ValueType::Function)},
  {"iter",                      Variable(ValueType::Function)},
  {"len",                       Variable(ValueType::Function)},
  {"license",                   Variable(ValueType::Function)},
  {"list",                      Variable(ValueType::Function)},
  {"locals",                    Variable(ValueType::Function)},
  {"map",                       Variable(ValueType::Function)},
  {"max",                       Variable(ValueType::Function)},
  {"memoryview",                Variable(ValueType::Function)},
  {"min",                       Variable(ValueType::Function)},
  {"next",                      Variable(ValueType::Function)},
  {"object",                    Variable(ValueType::Function)},
  {"oct",                       Variable(ValueType::Function)},
  {"open",                      Variable(ValueType::Function)},
  {"ord",                       Variable(ValueType::Function)},
  {"pow",                       Variable(ValueType::Function)},
  {"print",                     Variable(builtin_function_to_id.at("print"), false)},
  {"property",                  Variable(ValueType::Function)},
  {"quit",                      Variable(ValueType::Function)},
  {"range",                     Variable(ValueType::Function)},
  {"repr",                      Variable(ValueType::Function)},
  {"reversed",                  Variable(ValueType::Function)},
  {"round",                     Variable(ValueType::Function)},
  {"set",                       Variable(ValueType::Function)},
  {"setattr",                   Variable(ValueType::Function)},
  {"slice",                     Variable(ValueType::Function)},
  {"sorted",                    Variable(ValueType::Function)},
  {"staticmethod",              Variable(ValueType::Function)},
  {"str",                       Variable(ValueType::Function)},
  {"sum",                       Variable(ValueType::Function)},
  {"super",                     Variable(ValueType::Function)},
  {"tuple",                     Variable(ValueType::Function)},
  {"type",                      Variable(ValueType::Function)},
  {"vars",                      Variable(ValueType::Function)},
  {"zip",                       Variable(ValueType::Function)},
});
