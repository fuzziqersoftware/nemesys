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
#include "Modules/posix.hh"
#include "Modules/sys.hh"

using namespace std;
using FragDef = FunctionContext::BuiltinFunctionFragmentDefinition;



static BytesObject* empty_bytes = bytes_new(NULL, NULL, 0);
static UnicodeObject* empty_unicode = unicode_new(NULL, NULL, 0);



unordered_map<int64_t, FunctionContext> builtin_function_definitions;
unordered_map<int64_t, ClassContext> builtin_class_definitions;
unordered_map<string, Variable> builtin_names;



static int64_t generate_function_id() {
  // all builtin functions and classes have negative IDs
  static int64_t next_function_id = -1;
  return next_function_id--;
}

int64_t create_builtin_function(const char* name,
    const vector<Variable>& arg_types, const Variable& return_type,
    const void* compiled, bool register_globally) {
  vector<FragDef> defs({{arg_types, return_type, compiled}});
  return create_builtin_function(name, defs, register_globally);
}

int64_t create_builtin_function(const char* name,
    const vector<FragDef>& fragments, bool register_globally) {

  int64_t function_id = generate_function_id();

  builtin_function_definitions.emplace(piecewise_construct,
      forward_as_tuple(function_id), forward_as_tuple(nullptr, function_id, name, fragments));
  if (register_globally) {
    create_builtin_name(name, Variable(ValueType::Function, function_id));
  }

  return function_id;
}

int64_t create_builtin_class(const char* name,
    const map<string, Variable>& attributes,
    const vector<Variable>& init_arg_types, const void* init_compiled,
    const void* destructor, bool register_globally) {

  int64_t class_id = generate_function_id();

  // create and register the class context
  // TODO: define a built-in constructor that will do all this
  ClassContext& cls = builtin_class_definitions.emplace(piecewise_construct,
      forward_as_tuple(class_id), forward_as_tuple(nullptr, class_id)).first->second;
  cls.destructor = destructor;
  cls.name = name;
  cls.ast_root = NULL;
  cls.attributes = attributes;
  cls.populate_dynamic_attributes();

  // create and register __init__ if given
  // note that if __init__ is not given, the class can only be constructed from
  // nemesys internals, not by python code
  if (init_compiled) {
    Variable return_type(ValueType::Instance, class_id, NULL);
    vector<FragDef> defs({{init_arg_types, return_type, init_compiled}});
    FunctionContext& fn = builtin_function_definitions.emplace(piecewise_construct,
        forward_as_tuple(class_id), forward_as_tuple(nullptr, class_id, name, defs)).first->second;
    fn.class_id = class_id;

    if (register_globally) {
      create_builtin_name(name, Variable(ValueType::Class, class_id));
    }
  }

  return class_id;
}

void create_builtin_name(const char* name, const Variable& value) {
  builtin_names.emplace(name, value);
}



static void create_default_builtin_functions() {
  Variable None(ValueType::None);
  Variable Bool(ValueType::Bool);
  Variable Int(ValueType::Int);
  Variable Int_Zero(ValueType::Int, 0LL);
  Variable Float(ValueType::Float);
  Variable Float_Zero(ValueType::Float, 0.0);
  Variable Bytes(ValueType::Bytes);
  Variable Unicode(ValueType::Unicode);
  Variable Unicode_Blank(ValueType::Unicode, L"");
  Variable List_Any(ValueType::List, vector<Variable>({Variable()}));

  // None print(Unicode)
  create_builtin_function("print", {Unicode}, None,
      reinterpret_cast<const void*>(+[](UnicodeObject* str) {
    fprintf(stdout, "%.*ls\n", static_cast<int>(str->count), str->data);
    delete_reference(str);
  }), true);

  // Unicode input(Unicode='')
  create_builtin_function("input", {Unicode_Blank}, Unicode,
      reinterpret_cast<const void*>(+[](UnicodeObject* prompt) -> UnicodeObject* {
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
  }), true);

  // Int int(Int=0, Int=0)
  // Int int(Bytes, Int=0)
  // Int int(Unicode, Int=0)
  // Int int(Float, Int=0)
  create_builtin_function("int", {
      FragDef({Int_Zero, Int_Zero}, Int, reinterpret_cast<const void*>(+[](int64_t i, int64_t) -> int64_t {
    return i;
  })), FragDef({Bytes, Int_Zero}, Int, reinterpret_cast<const void*>(+[](BytesObject* s, int64_t base) -> int64_t {
    int64_t ret = strtoll(reinterpret_cast<const char*>(s->data), NULL, base);
    delete_reference(s);
    return ret;
  })), FragDef({Unicode, Int_Zero}, Int, reinterpret_cast<const void*>(+[](UnicodeObject* s, int64_t base) -> int64_t {
    int64_t ret = wcstoll(s->data, NULL, base);
    delete_reference(s);
    return ret;
  })), FragDef({Float, Int_Zero}, Int, reinterpret_cast<const void*>(+[](double x, int64_t) -> int64_t {
    return static_cast<int64_t>(x);
  }))}, true);

  // Float float(Float=0.0)
  // Float float(Int)
  // Float float(Bytes)
  // Float float(Unicode)
  create_builtin_function("float", {
      FragDef({Float_Zero}, Float, reinterpret_cast<const void*>(+[](double f) -> double {
    return f;
  })), FragDef({Int}, Float, reinterpret_cast<const void*>(+[](int64_t i) -> double {
    return static_cast<double>(i);
  })), FragDef({Bytes}, Float, reinterpret_cast<const void*>(+[](BytesObject* s) -> double {
    double ret = strtod(s->data, NULL);
    delete_reference(s);
    return ret;
  })), FragDef({Unicode}, Float, reinterpret_cast<const void*>(+[](UnicodeObject* s) -> double {
    double ret = wcstod(s->data, NULL);
    delete_reference(s);
    return ret;
  }))}, true);

  // Unicode repr(None)
  // Unicode repr(Bool)
  // Unicode repr(Int)
  // Unicode repr(Float)
  // Unicode repr(Bytes)
  // Unicode repr(Unicode)
  create_builtin_function("repr", {
       FragDef({None}, Unicode, reinterpret_cast<const void*>(+[](void*) -> UnicodeObject* {
    static UnicodeObject* ret = unicode_new(NULL, L"None", 4);
    add_reference(ret);
    return ret;

  })), FragDef({Bool}, Unicode, reinterpret_cast<const void*>(+[](bool v) -> UnicodeObject* {
    static UnicodeObject* true_str = unicode_new(NULL, L"True", 4);
    static UnicodeObject* false_str = unicode_new(NULL, L"False", 5);
    UnicodeObject* ret = v ? true_str : false_str;
    add_reference(ret);
    return ret;

  })), FragDef({Int}, Unicode, reinterpret_cast<const void*>(+[](int64_t v) -> UnicodeObject* {
    wchar_t buf[24];
    return unicode_new(NULL, buf, swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%" PRId64, v));

  })), FragDef({Float}, Unicode, reinterpret_cast<const void*>(+[](double v) -> UnicodeObject* {
    wchar_t buf[60]; // TODO: figure out how long this actually needs to be
    size_t count = swprintf(buf, sizeof(buf) / sizeof(buf[0]) - 2, L"%g", v);

    // if there isn't a . in the output, add .0 at the end
    size_t x;
    for (x = 0; x < count; x++) {
      if (buf[x] == L'.') {
        break;
      }
    }
    if (x == count) {
      buf[count] = L'.';
      buf[count + 1] = L'0';
      buf[count + 2] = 0;
    }

    return unicode_new(NULL, buf, wcslen(buf));

  })), FragDef({Bytes}, Unicode, reinterpret_cast<const void*>(+[](BytesObject* v) -> UnicodeObject* {
    string escape_ret = escape(reinterpret_cast<const char*>(v->data), v->count);
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

  })), FragDef({Unicode}, Unicode, reinterpret_cast<const void*>(+[](UnicodeObject* v) -> UnicodeObject* {
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
  }))}, true);

  // Int len(Bytes)
  // Int len(Unicode)
  // Int len(List[Any])
  // Int len(Tuple[...]) // unimplemented
  // Int len(Set[Any]) // unimplemented
  // Int len(Dict[Any, Any]) // unimplemented
  create_builtin_function("len", {
       FragDef({Bytes}, Int, reinterpret_cast<const void*>(+[](BytesObject* s) -> int64_t {
    int64_t ret = s->count;
    delete_reference(s);
    return ret;
  })), FragDef({Unicode}, Int, reinterpret_cast<const void*>(+[](UnicodeObject* s) -> int64_t {
    int64_t ret = s->count;
    delete_reference(s);
    return ret;
  })), FragDef({List_Any}, Int, reinterpret_cast<const void*>(+[](ListObject* l) -> int64_t {
    int64_t ret = l->count;
    delete_reference(l);
    return ret;
  }))}, true);

  // Int abs(Int)
  // Float abs(Float)
  // Float abs(Complex) // unimplemented
  create_builtin_function("abs", {
       FragDef({Int}, Int, reinterpret_cast<const void*>(+[](int64_t i) -> int64_t {
    return (i < 0) ? -i : i;
  })), FragDef({Float}, Float, reinterpret_cast<const void*>(+[](double i) -> double {
    return (i < 0) ? -i : i;
  }))}, true);

  // Unicode chr(Int)
  create_builtin_function("chr", {Int}, Unicode,
      reinterpret_cast<const void*>(+[](int64_t i) -> UnicodeObject* {
    UnicodeObject* s = unicode_new(NULL, NULL, 1);
    s->data[0] = i;
    s->data[1] = 0;
    return s;
  }), true);

  // Int ord(Bytes) // apparently this isn't part of the Python standard anymore
  // Int ord(Unicode)
  create_builtin_function("ord", {
       FragDef({Bytes}, Int, reinterpret_cast<const void*>(+[](BytesObject* s) -> int64_t {
    int64_t ret = (s->count < 1) ? -1 : s->data[0];
    delete_reference(s);
    return ret;
  })), FragDef({Unicode}, Int, reinterpret_cast<const void*>(+[](UnicodeObject* s) -> int64_t {
    int64_t ret = (s->count < 1) ? -1 : s->data[0];
    delete_reference(s);
    return ret;
  }))}, true);

  // Unicode bin(Int)
  create_builtin_function("bin", {Int}, Unicode,
      reinterpret_cast<const void*>(+[](int64_t i) -> UnicodeObject* {
    if (!i) {
      return unicode_new(NULL, L"0b0", 3);
    }

    UnicodeObject* s = unicode_new(NULL, NULL, 67);
    size_t x = 0;
    if (i < 0) {
      i = -i;
      s->data[x++] = '-';
    }
    s->data[x++] = '0';
    s->data[x++] = 'b';

    bool should_write = false;
    for (size_t y = 0; y < sizeof(int64_t) * 8; y++) {
      bool bit_set = i & 0x8000000000000000;
      if (bit_set) {
        should_write = true;
      }
      if (should_write) {
        s->data[x++] = bit_set ? '1' : '0';
      }
      i <<= 1;
    }
    s->data[x] = 0;
    s->count = x;
    return s;
  }), true);

  // Unicode hex(Int)
  create_builtin_function("hex", {Int}, Unicode,
      reinterpret_cast<const void*>(+[](int64_t i) -> UnicodeObject* {
    UnicodeObject* s = unicode_new(NULL, NULL, 19);
    s->count = swprintf(s->data, 19, L"%s0x%x", (i < 0) ? "-" : "", (i < 0) ? -i : i);
    return s;
  }), true);

  // unimplemented:
  //   __import__()
  //   all()
  //   any()
  //   ascii()
  //   bool()
  //   bytearray()
  //   bytes()
  //   callable()
  //   classmethod() // won't be implemented here (will be done statically)
  //   compile()
  //   complex()
  //   delattr()
  //   dict()
  //   dir()
  //   divmod()
  //   enumerate()
  //   eval()
  //   exec()
  //   filter()
  //   format()
  //   frozenset()
  //   getattr()
  //   globals()
  //   hasattr()
  //   hash()
  //   help()
  //   id()
  //   isinstance()
  //   issubclass()
  //   iter()
  //   list()
  //   locals()
  //   map()
  //   max()
  //   memoryview()
  //   min()
  //   next()
  //   object()
  //   oct()
  //   open()
  //   pow()
  //   property()
  //   range()
  //   reversed()
  //   round()
  //   set()
  //   setattr()
  //   slice()
  //   sorted()
  //   staticmethod() // won't be implemented here (will be done statically)
  //   str()
  //   sum()
  //   super()
  //   tuple()
  //   type()
  //   vars()
  //   zip()
}



/* TODO: finish implementing file objects here
struct FileObject {
  int64_t closed;
  FILE* f;
  UnicodeObject* mode;
  UnicodeObject* name;

  static FileObject* __init__(FileObject* f, UnicodeObject* filename,
      UnicodeObject* mode) {
    // TODO
    // return f;
  }

  static void __del__(FileObject* f) {
    if (!f->closed) {
      fclose(f->f);
    }
    delete_reference(f->mode);
  }

  static void close(FileObject* f) {
    fclose(f->f);
  }

  static int64_t fileno(FileObject* f) {
    return fileno(f->f);
  }

  static void flush(FileObject* f) {
    fflush(f->f);
  }

  static int64_t isatty(FileObject* f) {
    return isatty(fileno(f->f));
  }

  static BytesObject* read(FileObject* f, int64_t size) {
    // TODO
  }

  static BytesObject* read1(FileObject* f, int64_t size) {
    if (bytes <= 0) {
      return bytes_new(NULL, NULL, 0);
    }

    BytesObject* block = bytes_new(NULL, NULL, size);
    ssize_t bytes_read = fread(block->data, 1, size, f->f);
    if (bytes_read <= 0) {
      delete_reference(block);
      return bytes_new(NULL, NULL, size);
    }
    block->count = bytes_read;
    return block;
  }
};

void create_default_builtin_classes() {
  const map<string, Variable> file_attributes({
    // {"__enter__", Variable()},
    // {"__exit__", Variable()},
    // {"__iter__", Variable()},
    // {"__next__", Variable()},
    // {"__repr__", Variable()},
    // {"_checkClosed", Variable()},
    // {"_checkReadable", Variable()},
    // {"_checkSeekable", Variable()},
    // {"_checkWritable", Variable()},
    // {"_dealloc_warn", Variable()},
    // {"_finalizing", Variable()},
    {"close", Variable()},
    {"closed", Variable()},
    {"fileno", Variable()},
    {"flush", Variable()},
    {"isatty", Variable()},
    {"mode", Variable()},
    {"name", Variable()},
    {"read", Variable()},
    {"read1", Variable()},
    {"readline", Variable()},
    {"seek", Variable()},
    {"tell", Variable()},
    {"truncate", Variable()},
    {"write", Variable()},
  });
} */

void create_default_builtin_classes() { }

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
  create_default_builtin_classes();
}



struct BuiltinModule {
  bool initialized;
  void (*initialize)();

  // note: this has to be a function because the module pointers are statically
  // initialized, and there's no guarantee that builtin_modules isn't
  // initialized before they are
  shared_ptr<ModuleAnalysis> (*get_module)();

  BuiltinModule(void (*initialize)(), shared_ptr<ModuleAnalysis> (*get_module)())
      : initialized(false), initialize(initialize), get_module(get_module) { }
};

#define DECLARE_MODULE(name) {#name, {name##_initialize, []() -> shared_ptr<ModuleAnalysis> {return name##_module;}}}
static unordered_map<string, BuiltinModule> builtin_modules({
  DECLARE_MODULE(__nemesys__),
  DECLARE_MODULE(posix),
  DECLARE_MODULE(sys),
});
#undef DECLARE_MODULE

shared_ptr<ModuleAnalysis> get_builtin_module(const string& module_name) {
  try {
    auto& it = builtin_modules.at(module_name);
    if (!it.initialized) {
      it.initialize();
      it.initialized = true;
    }
    return it.get_module();
  } catch (const out_of_range& e) {
    return NULL;
  }
}
