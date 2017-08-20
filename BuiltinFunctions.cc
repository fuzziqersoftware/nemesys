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
#include "Modules/__nemesys__.hh"
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



unordered_map<string, int64_t> builtin_function_to_id;
unordered_map<int64_t, FunctionContext> builtin_function_definitions;
unordered_map<string, Variable> builtin_names;



using FragDef = FunctionContext::BuiltinFunctionFragmentDefinition;

int64_t create_builtin_function(const char* name,
    const vector<Variable>& arg_types, const Variable& return_type,
    const void* compiled, bool register_globally) {
  vector<FragDef> defs({{arg_types, return_type, compiled}});
  return create_builtin_function(name, defs, register_globally);
}

int64_t create_builtin_function(const char* name,
    const vector<FragDef>& fragments, bool register_globally) {
  // all builtin functions have negative function IDs
  static int64_t next_function_id = -1;
  int64_t function_id = next_function_id--;

  builtin_function_definitions.emplace(function_id, FunctionContext(NULL,
      function_id, name, fragments));
  if (register_globally) {
    create_builtin_name(name, Variable(ValueType::Function, function_id));
  }

  return function_id;
}

void create_builtin_name(const char* name, const Variable& value) {
  builtin_names.emplace(name, value);
}



void create_default_builtin_functions() {
  // None print(Unicode)
  create_builtin_function("print", {Variable(ValueType::Unicode)},
      Variable(ValueType::None), reinterpret_cast<const void*>(&builtin_print),
      true);

  // Unicode input(Unicode='')
  create_builtin_function("input", {Variable(ValueType::Unicode, L"")},
      Variable(ValueType::Unicode), reinterpret_cast<const void*>(&builtin_input),
      true);

  // Int int(Bytes, Int=0)
  // Int int(Unicode, Int=0)
  create_builtin_function("int", {
      FragDef({Variable(ValueType::Bytes), Variable(ValueType::Int, 0LL)},
        Variable(ValueType::Int), reinterpret_cast<const void*>(&builtin_int_bytes)),
      FragDef({Variable(ValueType::Unicode), Variable(ValueType::Int, 0LL)},
        Variable(ValueType::Int), reinterpret_cast<const void*>(&builtin_int_unicode)),
  }, true);

  // Unicode repr(None)
  // Unicode repr(Bool)
  // Unicode repr(Int)
  // Unicode repr(Float)
  // Unicode repr(Bytes)
  // Unicode repr(Unicode)
  create_builtin_function("repr", {
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
  }, true);

  // Int len(Bytes)
  // Int len(Unicode)
  // Int len(List[Any])
  create_builtin_function("len", {
      FragDef({Variable(ValueType::Bytes)}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(&builtin_len_bytes)),
      FragDef({Variable(ValueType::Unicode)}, Variable(ValueType::Int),
        reinterpret_cast<const void*>(&builtin_len_unicode)),
      FragDef({Variable(ValueType::List, vector<Variable>({Variable()}))},
        Variable(ValueType::Int),
        reinterpret_cast<const void*>(&builtin_len_list)),
  }, true);
}

void create_default_builtin_names() {
  static const unordered_map<Variable, shared_ptr<Variable>> empty_dict_contents;
  static const Variable empty_dict(ValueType::Dict, empty_dict_contents);

  create_builtin_name("__annotations__",           empty_dict);
  create_builtin_name("__build_class__",           Variable(ValueType::Function));
  create_builtin_name("__debug__",                 Variable(ValueType::Bool, true));
  create_builtin_name("__doc__",                   Variable(ValueType::None));
  create_builtin_name("__import__",                Variable(ValueType::Function));
  create_builtin_name("__loader__",                Variable(ValueType::None));
  create_builtin_name("__name__",                  Variable(ValueType::Unicode));
  create_builtin_name("__package__",               Variable(ValueType::None));
  create_builtin_name("__spec__",                  Variable(ValueType::None));
  create_builtin_name("ArithmeticError",           Variable(ValueType::Function));
  create_builtin_name("AssertionError",            Variable(ValueType::Function));
  create_builtin_name("AttributeError",            Variable(ValueType::Function));
  create_builtin_name("BaseException",             Variable(ValueType::Function));
  create_builtin_name("BlockingIOError",           Variable(ValueType::Function));
  create_builtin_name("BrokenPipeError",           Variable(ValueType::Function));
  create_builtin_name("BufferError",               Variable(ValueType::Function));
  create_builtin_name("BytesWarning",              Variable(ValueType::Function));
  create_builtin_name("ChildProcessError",         Variable(ValueType::Function));
  create_builtin_name("ConnectionAbortedError",    Variable(ValueType::Function));
  create_builtin_name("ConnectionError",           Variable(ValueType::Function));
  create_builtin_name("ConnectionRefusedError",    Variable(ValueType::Function));
  create_builtin_name("ConnectionResetError",      Variable(ValueType::Function));
  create_builtin_name("DeprecationWarning",        Variable(ValueType::Function));
  create_builtin_name("EOFError",                  Variable(ValueType::Function));
  create_builtin_name("Ellipsis",                  Variable());
  create_builtin_name("EnvironmentError",          Variable(ValueType::Function));
  create_builtin_name("Exception",                 Variable(ValueType::Function));
  create_builtin_name("FileExistsError",           Variable(ValueType::Function));
  create_builtin_name("FileNotFoundError",         Variable(ValueType::Function));
  create_builtin_name("FloatingPointError",        Variable(ValueType::Function));
  create_builtin_name("FutureWarning",             Variable(ValueType::Function));
  create_builtin_name("GeneratorExit",             Variable(ValueType::Function));
  create_builtin_name("IOError",                   Variable(ValueType::Function));
  create_builtin_name("ImportError",               Variable(ValueType::Function));
  create_builtin_name("ImportWarning",             Variable(ValueType::Function));
  create_builtin_name("IndentationError",          Variable(ValueType::Function));
  create_builtin_name("IndexError",                Variable(ValueType::Function));
  create_builtin_name("InterruptedError",          Variable(ValueType::Function));
  create_builtin_name("IsADirectoryError",         Variable(ValueType::Function));
  create_builtin_name("KeyError",                  Variable(ValueType::Function));
  create_builtin_name("KeyboardInterrupt",         Variable(ValueType::Function));
  create_builtin_name("LookupError",               Variable(ValueType::Function));
  create_builtin_name("MemoryError",               Variable(ValueType::Function));
  create_builtin_name("ModuleNotFoundError",       Variable(ValueType::Function));
  create_builtin_name("NameError",                 Variable(ValueType::Function));
  create_builtin_name("NotADirectoryError",        Variable(ValueType::Function));
  create_builtin_name("NotImplemented",            Variable());
  create_builtin_name("NotImplementedError",       Variable(ValueType::Function));
  create_builtin_name("OSError",                   Variable(ValueType::Function));
  create_builtin_name("OverflowError",             Variable(ValueType::Function));
  create_builtin_name("PendingDeprecationWarning", Variable(ValueType::Function));
  create_builtin_name("PermissionError",           Variable(ValueType::Function));
  create_builtin_name("ProcessLookupError",        Variable(ValueType::Function));
  create_builtin_name("RecursionError",            Variable(ValueType::Function));
  create_builtin_name("ReferenceError",            Variable(ValueType::Function));
  create_builtin_name("ResourceWarning",           Variable(ValueType::Function));
  create_builtin_name("RuntimeError",              Variable(ValueType::Function));
  create_builtin_name("RuntimeWarning",            Variable(ValueType::Function));
  create_builtin_name("StopAsyncIteration",        Variable(ValueType::Function));
  create_builtin_name("StopIteration",             Variable(ValueType::Function));
  create_builtin_name("SyntaxError",               Variable(ValueType::Function));
  create_builtin_name("SyntaxWarning",             Variable(ValueType::Function));
  create_builtin_name("SystemError",               Variable(ValueType::Function));
  create_builtin_name("SystemExit",                Variable(ValueType::Function));
  create_builtin_name("TabError",                  Variable(ValueType::Function));
  create_builtin_name("TimeoutError",              Variable(ValueType::Function));
  create_builtin_name("TypeError",                 Variable(ValueType::Function));
  create_builtin_name("UnboundLocalError",         Variable(ValueType::Function));
  create_builtin_name("UnicodeEncodeError",        Variable(ValueType::Function));
  create_builtin_name("UnicodeTranslateError",     Variable(ValueType::Function));
  create_builtin_name("UserWarning",               Variable(ValueType::Function));
  create_builtin_name("UnicodeDecodeError",        Variable(ValueType::Function));
  create_builtin_name("UnicodeError",              Variable(ValueType::Function));
  create_builtin_name("UnicodeWarning",            Variable(ValueType::Function));
  create_builtin_name("ValueError",                Variable(ValueType::Function));
  create_builtin_name("Warning",                   Variable(ValueType::Function));
  create_builtin_name("ZeroDivisionError",         Variable(ValueType::Function));
  create_builtin_name("abs",                       Variable(ValueType::Function));
  create_builtin_name("all",                       Variable(ValueType::Function));
  create_builtin_name("any",                       Variable(ValueType::Function));
  create_builtin_name("ascii",                     Variable(ValueType::Function));
  create_builtin_name("bin",                       Variable(ValueType::Function));
  create_builtin_name("bool",                      Variable(ValueType::Function));
  create_builtin_name("bytearray",                 Variable(ValueType::Function));
  create_builtin_name("bytes",                     Variable(ValueType::Function));
  create_builtin_name("callable",                  Variable(ValueType::Function));
  create_builtin_name("chr",                       Variable(ValueType::Function));
  create_builtin_name("classmethod",               Variable(ValueType::Function));
  create_builtin_name("compile",                   Variable(ValueType::Function));
  create_builtin_name("complex",                   Variable(ValueType::Function));
  create_builtin_name("copyright",                 Variable(ValueType::Function));
  create_builtin_name("credits",                   Variable(ValueType::Function));
  create_builtin_name("delattr",                   Variable(ValueType::Function));
  create_builtin_name("dict",                      Variable(ValueType::Function));
  create_builtin_name("dir",                       Variable(ValueType::Function));
  create_builtin_name("divmod",                    Variable(ValueType::Function));
  create_builtin_name("enumerate",                 Variable(ValueType::Function));
  create_builtin_name("eval",                      Variable(ValueType::Function));
  create_builtin_name("exec",                      Variable(ValueType::Function));
  create_builtin_name("exit",                      Variable(ValueType::Function));
  create_builtin_name("filter",                    Variable(ValueType::Function));
  create_builtin_name("float",                     Variable(ValueType::Function));
  create_builtin_name("format",                    Variable(ValueType::Function));
  create_builtin_name("frozenset",                 Variable(ValueType::Function));
  create_builtin_name("getattr",                   Variable(ValueType::Function));
  create_builtin_name("globals",                   Variable(ValueType::Function));
  create_builtin_name("hasattr",                   Variable(ValueType::Function));
  create_builtin_name("hash",                      Variable(ValueType::Function));
  create_builtin_name("help",                      Variable(ValueType::Function));
  create_builtin_name("hex",                       Variable(ValueType::Function));
  create_builtin_name("id",                        Variable(ValueType::Function));
  create_builtin_name("isinstance",                Variable(ValueType::Function));
  create_builtin_name("issubclass",                Variable(ValueType::Function));
  create_builtin_name("iter",                      Variable(ValueType::Function));
  create_builtin_name("license",                   Variable(ValueType::Function));
  create_builtin_name("list",                      Variable(ValueType::Function));
  create_builtin_name("locals",                    Variable(ValueType::Function));
  create_builtin_name("map",                       Variable(ValueType::Function));
  create_builtin_name("max",                       Variable(ValueType::Function));
  create_builtin_name("memoryview",                Variable(ValueType::Function));
  create_builtin_name("min",                       Variable(ValueType::Function));
  create_builtin_name("next",                      Variable(ValueType::Function));
  create_builtin_name("object",                    Variable(ValueType::Function));
  create_builtin_name("oct",                       Variable(ValueType::Function));
  create_builtin_name("open",                      Variable(ValueType::Function));
  create_builtin_name("ord",                       Variable(ValueType::Function));
  create_builtin_name("pow",                       Variable(ValueType::Function));
  create_builtin_name("property",                  Variable(ValueType::Function));
  create_builtin_name("quit",                      Variable(ValueType::Function));
  create_builtin_name("range",                     Variable(ValueType::Function));
  create_builtin_name("reversed",                  Variable(ValueType::Function));
  create_builtin_name("round",                     Variable(ValueType::Function));
  create_builtin_name("set",                       Variable(ValueType::Function));
  create_builtin_name("setattr",                   Variable(ValueType::Function));
  create_builtin_name("slice",                     Variable(ValueType::Function));
  create_builtin_name("sorted",                    Variable(ValueType::Function));
  create_builtin_name("staticmethod",              Variable(ValueType::Function));
  create_builtin_name("str",                       Variable(ValueType::Function));
  create_builtin_name("sum",                       Variable(ValueType::Function));
  create_builtin_name("super",                     Variable(ValueType::Function));
  create_builtin_name("tuple",                     Variable(ValueType::Function));
  create_builtin_name("type",                      Variable(ValueType::Function));
  create_builtin_name("vars",                      Variable(ValueType::Function));
  create_builtin_name("zip",                       Variable(ValueType::Function));

  create_default_builtin_functions();
}

shared_ptr<ModuleAnalysis> get_builtin_module(const string& module_name) {
  if (module_name == "__nemesys__") {
    static bool initialized = false;
    if (!initialized) {
      __nemesys___initialize();
      initialized = true;
    }
    return __nemesys___module;
  }
  if (module_name == "sys") {
    static bool initialized = false;
    if (!initialized) {
      sys_initialize();
      initialized = true;
    }
    return sys_module;
  }
  return NULL;
}
