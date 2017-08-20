#include "BuiltinFunctions.hh"

#include <inttypes.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Analysis.hh"
#include "Types/Strings.hh"
#include "Types/List.hh"
#include "PythonLexer.hh" // for escape()

// builtin module implementations
#include "Modules/sys.hh"

using namespace std;



static BytesObject* empty_bytes = bytes_new(NULL, NULL, 0);
static UnicodeObject* empty_unicode = unicode_new(NULL, NULL, 0);

static void builtin_print(UnicodeObject* str) {
  fprintf(stdout, "%.*ls\n", static_cast<int>(str->count), str->data);
  delete_reference(str);
}

static UnicodeObject* builtin_input(UnicodeObject* prompt) {
  if (prompt->count) {
    fprintf(stdout, "%.*ls", static_cast<int>(prompt->count), prompt->data);
    fflush(stdout);
  }
  delete_reference(prompt);

  vector<wstring> blocks;
  while (blocks.empty() || blocks.back().back() != '\n') {
    blocks.emplace_back(0x400, 0);
    if (!fgetws(const_cast<wchar_t*>(blocks.back().data()), blocks.back().size(), stdin)) {
      blocks.pop_back();
      break;
    }
    blocks.back().resize(wcslen(blocks.back().c_str()));
  }

  if (blocks.empty()) {
    add_reference(empty_unicode);
    return empty_unicode;
  }

  // concatenate the blocks
  wstring data;
  if (blocks.size() == 1) {
    data = move(blocks[0]);
  } else {
    for (const auto& block : blocks) {
      data += block;
    }
  }

  // trim off the trailing newline
  if (!data.empty() && (data.back() == '\n')) {
    data.resize(data.size() - 1);
  }

  if (data.empty()) {
    add_reference(empty_unicode);
    return empty_unicode;
  }
  return unicode_new(NULL, data.data(), data.size());
}

static int64_t builtin_int_bytes(BytesObject* s, int64_t base) {
  int64_t ret = strtoll(reinterpret_cast<const char*>(s->data), NULL, base);
  delete_reference(s);
  return ret;
}

static int64_t builtin_int_unicode(UnicodeObject* s, int64_t base) {
  int64_t ret = wcstoll(s->data, NULL, base);
  delete_reference(s);
  return ret;
}

static UnicodeObject* builtin_repr_none(void* None) {
  static UnicodeObject* ret = unicode_new(NULL, L"None", 4);
  add_reference(ret);
  return ret;
}

static UnicodeObject* builtin_repr_bool(bool v) {
  static UnicodeObject* true_str = unicode_new(NULL, L"True", 4);
  static UnicodeObject* false_str = unicode_new(NULL, L"False", 5);
  UnicodeObject* ret = v ? true_str : false_str;
  add_reference(ret);
  return ret;
}

static UnicodeObject* builtin_repr_int(int64_t v) {
  wchar_t buf[24];
  swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%" PRId64, v);
  return unicode_new(NULL, buf, wcslen(buf));
}

static UnicodeObject* builtin_repr_float(double v) {
  wchar_t buf[60]; // TODO: figure out how long this actually needs to be
  swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%g", v);
  return unicode_new(NULL, buf, wcslen(buf));
}

static UnicodeObject* builtin_repr_bytes(BytesObject* v) {
  string escape_ret = escape(v->data, v->count);
  UnicodeObject* ret = unicode_new(NULL, NULL, escape_ret.size() + 3);
  ret->data[0] = L'b';
  ret->data[1] = L'\'';
  for (size_t x = 0; x < escape_ret.size(); x++) {
    ret->data[x + 2] = escape_ret[x];
  }
  ret->data[escape_ret.size() + 2] = L'\'';
  ret->data[escape_ret.size() + 3] = 0;
  delete_reference(v);
  return ret;
}

static UnicodeObject* builtin_repr_unicode(UnicodeObject* v) {
  string escape_ret = escape(v->data, v->count);
  UnicodeObject* ret = unicode_new(NULL, NULL, escape_ret.size() + 2);
  ret->data[0] = L'\'';
  for (size_t x = 0; x < escape_ret.size(); x++) {
    ret->data[x + 1] = escape_ret[x];
  }
  ret->data[escape_ret.size() + 1] = L'\'';
  ret->data[escape_ret.size() + 2] = 0;
  delete_reference(v);
  return ret;
}

static int64_t builtin_len_bytes(BytesObject* s) {
  int64_t ret = s->count;
  delete_reference(s);
  return ret;
}

static int64_t builtin_len_unicode(UnicodeObject* s) {
  int64_t ret = s->count;
  delete_reference(s);
  return ret;
}

static int64_t builtin_len_list(ListObject* l) {
  int64_t ret = l->count;
  delete_reference(l);
  return ret;
}



// all builtin functions have negative function IDs
const unordered_map<string, int64_t> builtin_function_to_id({
  {"print", -1},
  {"input", -2},
  {"int",   -3},
  {"repr",  -4},
  {"len",   -5},
});



using FragDef = FunctionContext::BuiltinFunctionFragmentDefinition;

unordered_map<int64_t, FunctionContext> builtin_function_definitions({
  // None print(Unicode)
  {builtin_function_to_id.at("print"),
    FunctionContext(NULL, builtin_function_to_id.at("print"), "print",
        {Variable(ValueType::Unicode)}, Variable(ValueType::None),
        reinterpret_cast<const void*>(&builtin_print))},
  // Unicode input(Unicode='')
  {builtin_function_to_id.at("input"),
    FunctionContext(NULL, builtin_function_to_id.at("input"), "input",
        {Variable(ValueType::Unicode, L"")}, Variable(ValueType::Unicode),
        reinterpret_cast<const void*>(&builtin_input))},
  // Int int(Bytes, Int=0)
  // Int int(Unicode, Int=0)
  {builtin_function_to_id.at("int"),
    FunctionContext(NULL, builtin_function_to_id.at("int"), "int", {
      FragDef({Variable(ValueType::Bytes), Variable(ValueType::Int, 0LL)},
        Variable(ValueType::Int), reinterpret_cast<const void*>(&builtin_int_bytes)),
      FragDef({Variable(ValueType::Unicode), Variable(ValueType::Int, 0LL)},
        Variable(ValueType::Int), reinterpret_cast<const void*>(&builtin_int_unicode)),
    })},
  // Unicode repr(None)
  // Unicode repr(Bool)
  // Unicode repr(Int)
  // Unicode repr(Float)
  // Unicode repr(Bytes)
  // Unicode repr(Unicode)
  {builtin_function_to_id.at("repr"),
    FunctionContext(NULL, builtin_function_to_id.at("int"), "int", {
      FragDef({Variable(ValueType::None)}, Variable(ValueType::Unicode),
        reinterpret_cast<const void*>(&builtin_repr_none)),
      FragDef({Variable(ValueType::Bool)}, Variable(ValueType::Unicode),
        reinterpret_cast<const void*>(&builtin_repr_bool)),
      FragDef({Variable(ValueType::Int)}, Variable(ValueType::Unicode),
        reinterpret_cast<const void*>(&builtin_repr_int)),
      FragDef({Variable(ValueType::Float)}, Variable(ValueType::Unicode),
        reinterpret_cast<const void*>(&builtin_repr_float)),
      FragDef({Variable(ValueType::Bytes)}, Variable(ValueType::Unicode),
        reinterpret_cast<const void*>(&builtin_repr_bytes)),
      FragDef({Variable(ValueType::Unicode)}, Variable(ValueType::Unicode),
        reinterpret_cast<const void*>(&builtin_repr_unicode)),
    })},
  // Int len(Bytes)
  // Int len(Unicode)
  // Int len(List[Any])
  {builtin_function_to_id.at("len"),
    FunctionContext(NULL, builtin_function_to_id.at("len"), "len", {
      FragDef({Variable(ValueType::Bytes)}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(&builtin_len_bytes)),
      FragDef({Variable(ValueType::Unicode)}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(&builtin_len_unicode)),
      FragDef({Variable(ValueType::List, vector<Variable>({Variable()}))},
        Variable(ValueType::Int),
        reinterpret_cast<const void*>(&builtin_len_list)),
    })},
});



unordered_map<Variable, shared_ptr<Variable>> empty_dict_contents;
static const Variable empty_dict(ValueType::Dict, empty_dict_contents);



// TODO: we'll have to do more than this in the future for builtins
const unordered_map<string, Variable> builtin_names({
  {"__annotations__",           empty_dict},
  {"__build_class__",           Variable(ValueType::Function)},
  {"__debug__",                 Variable(ValueType::Bool, true)},
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
  {"input",                     Variable(ValueType::Function, builtin_function_to_id.at("input"))},
  {"int",                       Variable(ValueType::Function, builtin_function_to_id.at("int"))},
  {"isinstance",                Variable(ValueType::Function)},
  {"issubclass",                Variable(ValueType::Function)},
  {"iter",                      Variable(ValueType::Function)},
  {"len",                       Variable(ValueType::Function, builtin_function_to_id.at("len"))},
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
  {"print",                     Variable(ValueType::Function, builtin_function_to_id.at("print"))},
  {"property",                  Variable(ValueType::Function)},
  {"quit",                      Variable(ValueType::Function)},
  {"range",                     Variable(ValueType::Function)},
  {"repr",                      Variable(ValueType::Function, builtin_function_to_id.at("repr"))},
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

std::shared_ptr<ModuleAnalysis> get_builtin_module(const std::string& module_name) {
  if (module_name == "sys") {
    return sys_module;
  }
  return NULL;
}
