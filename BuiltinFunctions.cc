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



unordered_map<int64_t, FunctionContext> builtin_function_definitions;
unordered_map<int64_t, ClassContext> builtin_class_definitions;
unordered_map<string, Variable> builtin_names;
int64_t AssertionError_class_id;
int64_t MemoryError_class_id;



static BytesObject* empty_bytes = bytes_new(NULL, NULL, 0);
static UnicodeObject* empty_unicode = unicode_new(NULL, NULL, 0);
static Variable None(ValueType::None);
static Variable Bool(ValueType::Bool);
static Variable Bool_True(ValueType::Bool, true);
static Variable Bool_False(ValueType::Bool, false);
static Variable Int(ValueType::Int);
static Variable Int_Zero(ValueType::Int, 0LL);
static Variable Float(ValueType::Float);
static Variable Float_Zero(ValueType::Float, 0.0);
static Variable Bytes(ValueType::Bytes);
static Variable Unicode(ValueType::Unicode);
static Variable Unicode_Blank(ValueType::Unicode, L"");
static Variable List_Any(ValueType::List, vector<Variable>({Variable()}));



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

    // the first argument to __init__ is the class object, but we don't expect
    // the caller to provide this (they don't know the class id yet)
    auto modified_init_args = init_arg_types;
    modified_init_args.emplace(modified_init_args.begin(), return_type);

    // create the fragment definition
    vector<FragDef> defs({{modified_init_args, return_type, init_compiled}});
    FunctionContext& fn = builtin_function_definitions.emplace(piecewise_construct,
        forward_as_tuple(class_id),
        forward_as_tuple(nullptr, class_id, name, defs)).first->second;
    fn.class_id = class_id;
  }

  if (register_globally) {
    create_builtin_name(name, Variable(ValueType::Class, class_id));
  }

  return class_id;
}

void create_builtin_name(const char* name, const Variable& value) {
  builtin_names.emplace(name, value);
}



static void create_default_builtin_functions() {
  // None print(None)
  // None print(Bool)
  // None print(Int)
  // None print(Float)
  // None print(Bytes)
  // None print(Unicode)
  create_builtin_function("print", {
      FragDef({None}, None, reinterpret_cast<const void*>(+[](void*) {
    fwrite("None\n", 5, 1, stdout);

  })), FragDef({Bool}, None, reinterpret_cast<const void*>(+[](bool v) {
    if (v) {
      fwrite("True\n", 5, 1, stdout);
    } else {
      fwrite("False\n", 6, 1, stdout);
    }

  })), FragDef({Int}, None, reinterpret_cast<const void*>(+[](int64_t v) {
    fprintf(stdout, "%" PRId64 "\n", v);

  })), FragDef({Float}, None, reinterpret_cast<const void*>(+[](double v) {
    fprintf(stdout, "%lf\n", v);

  })), FragDef({Bytes}, None, reinterpret_cast<const void*>(+[](BytesObject* str) {
    fprintf(stdout, "%.*s\n", static_cast<int>(str->count), str->data);
    delete_reference(str);

  })), FragDef({Unicode}, None, reinterpret_cast<const void*>(+[](UnicodeObject* str) {
    fprintf(stdout, "%.*ls\n", static_cast<int>(str->count), str->data);
    delete_reference(str);
  }))}, true);

  create_builtin_function("bool", {
      FragDef({Bool_False}, Bool, reinterpret_cast<const void*>(+[](bool b) -> bool {
    return b;

  })), FragDef({Int}, Bool, reinterpret_cast<const void*>(+[](int64_t i) -> bool {
    return static_cast<bool>(i);

  })), FragDef({Float}, Bool, reinterpret_cast<const void*>(+[](double f) -> bool {
    return (f != 0.0) && (f != -0.0);

  })), FragDef({Bytes}, Bool, reinterpret_cast<const void*>(+[](BytesObject* b) -> bool {
    bool ret = b->count != 0;
    delete_reference(b);
    return ret;

  })), FragDef({Unicode}, Bool, reinterpret_cast<const void*>(+[](UnicodeObject* u) -> bool {
    bool ret = u->count != 0;
    delete_reference(u);
    return ret;

  })), FragDef({List_Any}, Int, reinterpret_cast<const void*>(+[](ListObject* l) -> bool {
    bool ret = l->count != 0;
    delete_reference(l);
    return ret;
  }))}, true);

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

  // Bool bool(Bool=False)
  // Bool bool(Int)
  // Bool bool(Float)
  // Bool bool(Bytes)
  // Bool bool(Unicode)
  // Bool bool(List[Any])
  // Bool bool(Tuple[...]) // unimplemented
  // Bool bool(Set[Any]) // unimplemented
  // Bool bool(Dict[Any, Any]) // unimplemented
  // probably more that I'm forgetting right now
  create_builtin_function("bool", {
      FragDef({Bool_False}, Bool, reinterpret_cast<const void*>(+[](bool b) -> bool {
    return b;

  })), FragDef({Int}, Bool, reinterpret_cast<const void*>(+[](int64_t i) -> bool {
    return static_cast<bool>(i);

  })), FragDef({Float}, Bool, reinterpret_cast<const void*>(+[](double f) -> bool {
    return (f != 0.0) && (f != -0.0);

  })), FragDef({Bytes}, Bool, reinterpret_cast<const void*>(+[](BytesObject* b) -> bool {
    bool ret = b->count != 0;
    delete_reference(b);
    return ret;

  })), FragDef({Unicode}, Bool, reinterpret_cast<const void*>(+[](UnicodeObject* u) -> bool {
    bool ret = u->count != 0;
    delete_reference(u);
    return ret;

  })), FragDef({List_Any}, Int, reinterpret_cast<const void*>(+[](ListObject* l) -> bool {
    bool ret = l->count != 0;
    delete_reference(l);
    return ret;
  }))}, true);

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
      s->data[x++] = L'-';
    }
    s->data[x++] = L'0';
    s->data[x++] = L'b';

    bool should_write = false;
    for (size_t y = 0; y < sizeof(int64_t) * 8; y++) {
      bool bit_set = i & 0x8000000000000000;
      if (bit_set) {
        should_write = true;
      }
      if (should_write) {
        s->data[x++] = bit_set ? L'1' : L'0';
      }
      i <<= 1;
    }
    s->data[x] = 0;
    s->count = x;
    return s;
  }), true);

  // Unicode oct(Int)
  create_builtin_function("oct", {Int}, Unicode,
      reinterpret_cast<const void*>(+[](int64_t i) -> UnicodeObject* {
    if (!i) {
      return unicode_new(NULL, L"0o0", 3);
    }

    // this value is its own negative, so special-case it here so we can assume
    // below that the sign bit is never set
    if (i == 0x8000000000000000) {
      return unicode_new(NULL, L"-0o1000000000000000000000", 25);
    }

    UnicodeObject* s = unicode_new(NULL, NULL, 25);
    size_t x = 0;
    if (i < 0) {
      i = -i;
      s->data[x++] = '-';
    }
    s->data[x++] = L'0';
    s->data[x++] = L'o';

    i <<= 1;
    bool should_write = false;
    for (ssize_t y = 63; y > 0; y -= 3) {
      uint8_t value = ((i >> 61) & 7);
      if (value != 0) {
        should_write = true;
      }
      if (should_write) {
        s->data[x++] = L'0' + value;
      }
      i <<= 3;
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
}; */

void create_default_builtin_classes() {
  /* const map<string, Variable> file_attributes({
    {"__enter__", Variable()},
    {"__exit__", Variable()},
    {"__iter__", Variable()},
    {"__next__", Variable()},
    {"__repr__", Variable()},
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
  }); */

  auto one_field_constructor = reinterpret_cast<const void*>(
    +[](uint8_t* o, int64_t value) -> void* {
      // no need to deal with references; the reference passed to this function
      // becomes owned by the instance object
      *reinterpret_cast<int64_t*>(o + ClassContext::attribute_offset) = value;
      return o;
    });
  auto one_field_reference_destructor = reinterpret_cast<const void*>(
    +[](uint8_t* o) {
      delete_reference(*reinterpret_cast<void**>(o + ClassContext::attribute_offset));
      delete_reference(o);
    });

  auto declare_unimplemented_exception = +[](const char* name) -> int64_t {
    return create_builtin_class(name, {}, {}, NULL,
        reinterpret_cast<const void*>(&free), true);
  };

  AssertionError_class_id = create_builtin_class("AssertionError",
    {{"message", Unicode_Blank}}, {Unicode_Blank}, one_field_constructor,
    one_field_reference_destructor, true);

  declare_unimplemented_exception("ArithmeticError");
  declare_unimplemented_exception("AttributeError");
  declare_unimplemented_exception("BaseException");
  declare_unimplemented_exception("BlockingIOError");
  declare_unimplemented_exception("BrokenPipeError");
  declare_unimplemented_exception("BufferError");
  declare_unimplemented_exception("BytesWarning");
  declare_unimplemented_exception("ChildProcessError");
  declare_unimplemented_exception("ConnectionAbortedError");
  declare_unimplemented_exception("ConnectionError");
  declare_unimplemented_exception("ConnectionRefusedError");
  declare_unimplemented_exception("ConnectionResetError");
  declare_unimplemented_exception("DeprecationWarning");
  declare_unimplemented_exception("EOFError");
  declare_unimplemented_exception("EnvironmentError");
  declare_unimplemented_exception("Exception");
  declare_unimplemented_exception("FileExistsError");
  declare_unimplemented_exception("FileNotFoundError");
  declare_unimplemented_exception("FloatingPointError");
  declare_unimplemented_exception("FutureWarning");
  declare_unimplemented_exception("GeneratorExit");
  declare_unimplemented_exception("IOError");
  declare_unimplemented_exception("ImportError");
  declare_unimplemented_exception("ImportWarning");
  declare_unimplemented_exception("IndentationError");
  declare_unimplemented_exception("IndexError");
  declare_unimplemented_exception("InterruptedError");
  declare_unimplemented_exception("IsADirectoryError");
  declare_unimplemented_exception("KeyError");
  declare_unimplemented_exception("KeyboardInterrupt");
  declare_unimplemented_exception("LookupError");
  declare_unimplemented_exception("MemoryError");
  declare_unimplemented_exception("ModuleNotFoundError");
  declare_unimplemented_exception("NameError");
  declare_unimplemented_exception("NotADirectoryError");
  declare_unimplemented_exception("NotImplementedError");
  declare_unimplemented_exception("OSError");
  declare_unimplemented_exception("OverflowError");
  declare_unimplemented_exception("PendingDeprecationWarning");
  declare_unimplemented_exception("PermissionError");
  declare_unimplemented_exception("ProcessLookupError");
  declare_unimplemented_exception("RecursionError");
  declare_unimplemented_exception("ReferenceError");
  declare_unimplemented_exception("ResourceWarning");
  declare_unimplemented_exception("RuntimeError");
  declare_unimplemented_exception("RuntimeWarning");
  declare_unimplemented_exception("StopAsyncIteration");
  declare_unimplemented_exception("StopIteration");
  declare_unimplemented_exception("SyntaxError");
  declare_unimplemented_exception("SyntaxWarning");
  declare_unimplemented_exception("SystemError");
  declare_unimplemented_exception("SystemExit");
  declare_unimplemented_exception("TabError");
  declare_unimplemented_exception("TimeoutError");
  declare_unimplemented_exception("TypeError");
  declare_unimplemented_exception("UnboundLocalError");
  declare_unimplemented_exception("UnicodeEncodeError");
  declare_unimplemented_exception("UnicodeTranslateError");
  declare_unimplemented_exception("UserWarning");
  declare_unimplemented_exception("UnicodeDecodeError");
  declare_unimplemented_exception("UnicodeError");
  declare_unimplemented_exception("UnicodeWarning");
  declare_unimplemented_exception("ValueError");
  declare_unimplemented_exception("Warning");
  declare_unimplemented_exception("ZeroDivisionError");
}

void create_default_builtin_names() {
  static const unordered_map<Variable, shared_ptr<Variable>> empty_dict_contents;
  static const Variable empty_dict(ValueType::Dict, empty_dict_contents);

  create_builtin_name("__annotations__",           empty_dict);
  create_builtin_name("__build_class__",           Variable(ValueType::Function));
  create_builtin_name("__debug__",                 Variable(ValueType::Bool, true));
  create_builtin_name("__import__",                Variable(ValueType::Function));
  create_builtin_name("__loader__",                Variable(ValueType::None));
  create_builtin_name("__package__",               Variable(ValueType::None));
  create_builtin_name("__spec__",                  Variable(ValueType::None));
  create_builtin_name("Ellipsis",                  Variable());
  create_builtin_name("NotImplemented",            Variable());
  create_builtin_name("all",                       Variable(ValueType::Function));
  create_builtin_name("any",                       Variable(ValueType::Function));
  create_builtin_name("ascii",                     Variable(ValueType::Function));
  create_builtin_name("bool",                      Variable(ValueType::Function));
  create_builtin_name("bytearray",                 Variable(ValueType::Function));
  create_builtin_name("bytes",                     Variable(ValueType::Function));
  create_builtin_name("callable",                  Variable(ValueType::Function));
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
