#include "builtins.hh"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

#include <phosg/Strings.hh>

#include "../AST/PythonLexer.hh" // for escape()
#include "../Compiler/Contexts.hh"
#include "../Compiler/BuiltinFunctions.hh"
#include "../Types/List.hh"
#include "../Types/Tuple.hh"
#include "../Types/Strings.hh"
#include "../Types/Dictionary.hh"

using namespace std;
using FragDef = BuiltinFragmentDefinition;



extern shared_ptr<GlobalContext> global;

static UnicodeObject* empty_unicode = unicode_new(NULL, 0);
static const Value None(ValueType::None);
static const Value Bool(ValueType::Bool);
static const Value Bool_True(ValueType::Bool, true);
static const Value Bool_False(ValueType::Bool, false);
static const Value Int(ValueType::Int);
static const Value Int_Zero(ValueType::Int, static_cast<int64_t>(0));
static const Value Int_NegOne(ValueType::Int, static_cast<int64_t>(-1));
static const Value Float(ValueType::Float);
static const Value Float_Zero(ValueType::Float, 0.0);
static const Value Bytes(ValueType::Bytes);
static const Value Unicode(ValueType::Unicode);
static const Value Unicode_Blank(ValueType::Unicode, L"");
static const Value Extension0(ValueType::ExtensionTypeReference, static_cast<int64_t>(0));
static const Value Extension1(ValueType::ExtensionTypeReference, static_cast<int64_t>(1));
static const Value Self(ValueType::Instance, 0LL, nullptr);
static const Value List_Any(ValueType::List, vector<Value>({Value()}));
static const Value List_Same(ValueType::List, vector<Value>({Extension0}));
static const Value Set_Any(ValueType::Set, vector<Value>({Value()}));
static const Value Set_Same(ValueType::Set, vector<Value>({Extension0}));
static const Value Dict_Any(ValueType::Dict, vector<Value>({Value(), Value()}));
static const Value Dict_Same(ValueType::Dict, vector<Value>({Extension0, Extension1}));

static wstring __doc__ = L"Definitions of built-in functions.";

static map<string, Value> globals({
  {"__doc__",     Value(ValueType::Unicode, __doc__)},
  {"__name__",    Value(ValueType::Unicode, L"builtins")},

  // stuff which is still missing/unimplemented
  // {"__annotations__", Value(ValueType::Dict)},
  // {"__build_class__", Value(ValueType::Function)},
  {"__debug__",       Value(ValueType::Bool, true)},
  // {"__import__",      Value(ValueType::Function)},
  {"__loader__",      Value(ValueType::None)},
  {"__package__",     Value(ValueType::None)},
  {"__spec__",        Value(ValueType::None)},
  // {"Ellipsis",        Value()},
  // {"NotImplemented",  Value()},
  // {"all",             Value(ValueType::Function)},
  // {"any",             Value(ValueType::Function)},
  // {"ascii",           Value(ValueType::Function)},
  // {"bytearray",       Value(ValueType::Function)},
  // {"callable",        Value(ValueType::Function)},
  // {"classmethod",     Value(ValueType::Function)},
  // {"compile",         Value(ValueType::Function)},
  // {"complex",         Value(ValueType::Function)},
  // {"copyright",       Value(ValueType::Function)},
  // {"credits",         Value(ValueType::Function)},
  // {"delattr",         Value(ValueType::Function)},
  // {"dir",             Value(ValueType::Function)},
  // {"divmod",          Value(ValueType::Function)},
  // {"enumerate",       Value(ValueType::Function)},
  // {"eval",            Value(ValueType::Function)},
  // {"exec",            Value(ValueType::Function)},
  // {"exit",            Value(ValueType::Function)},
  // {"filter",          Value(ValueType::Function)},
  // {"format",          Value(ValueType::Function)},
  // {"frozenset",       Value(ValueType::Function)},
  // {"getattr",         Value(ValueType::Function)},
  // {"globals",         Value(ValueType::Function)},
  // {"hasattr",         Value(ValueType::Function)},
  // {"hash",            Value(ValueType::Function)},
  // {"help",            Value(ValueType::Function)},
  // {"id",              Value(ValueType::Function)},
  // {"isinstance",      Value(ValueType::Function)},
  // {"issubclass",      Value(ValueType::Function)},
  // {"iter",            Value(ValueType::Function)},
  // {"license",         Value(ValueType::Function)},
  // {"locals",          Value(ValueType::Function)},
  // {"map",             Value(ValueType::Function)},
  // {"max",             Value(ValueType::Function)},
  // {"memoryview",      Value(ValueType::Function)},
  // {"min",             Value(ValueType::Function)},
  // {"next",            Value(ValueType::Function)},
  // {"object",          Value(ValueType::Function)},
  // {"open",            Value(ValueType::Function)},
  // {"ord",             Value(ValueType::Function)},
  // {"pow",             Value(ValueType::Function)},
  // {"property",        Value(ValueType::Function)},
  // {"quit",            Value(ValueType::Function)},
  // {"range",           Value(ValueType::Function)},
  // {"reversed",        Value(ValueType::Function)},
  // {"round",           Value(ValueType::Function)},
  // {"setattr",         Value(ValueType::Function)},
  // {"slice",           Value(ValueType::Function)},
  // {"sorted",          Value(ValueType::Function)},
  // {"staticmethod",    Value(ValueType::Function)},
  // {"str",             Value(ValueType::Function)},
  // {"sum",             Value(ValueType::Function)},
  // {"super",           Value(ValueType::Function)},
  // {"type",            Value(ValueType::Function)},
  // {"vars",            Value(ValueType::Function)},
  // {"zip",             Value(ValueType::Function)},
});

shared_ptr<ModuleContext> builtins_initialize(GlobalContext* global_context) {

  static vector<BuiltinFunctionDefinition> function_defs({

    // None print(None)
    // None print(Bool)
    // None print(Int)
    // None print(Float)
    // None print(Bytes)
    // None print(Unicode)
    {"print", {FragDef({None}, None, void_fn_ptr([](void*) {
      fwrite("None\n", 5, 1, stdout);

    })), FragDef({Bool}, None, void_fn_ptr([](bool v) {
      if (v) {
        fwrite("True\n", 5, 1, stdout);
      } else {
        fwrite("False\n", 6, 1, stdout);
      }

    })), FragDef({Int}, None, void_fn_ptr([](int64_t v) {
      fprintf(stdout, "%" PRId64 "\n", v);

    })), FragDef({Float}, None, void_fn_ptr([](double v) {
      fprintf(stdout, "%lg\n", v);

    })), FragDef({Bytes}, None, void_fn_ptr([](BytesObject* str) {
      fprintf(stdout, "%.*s\n", static_cast<int>(str->count), str->data);
      delete_reference(str);

    })), FragDef({Unicode}, None, void_fn_ptr([](UnicodeObject* str) {
      fprintf(stdout, "%.*ls\n", static_cast<int>(str->count), str->data);
      delete_reference(str);
    }))}, false},

    {"bool", {FragDef({Bool_False}, Bool, void_fn_ptr([](bool b) -> bool {
      return b;

    })), FragDef({Int}, Bool, void_fn_ptr([](int64_t i) -> bool {
      return static_cast<bool>(i);

    })), FragDef({Float}, Bool, void_fn_ptr([](double f) -> bool {
      return (f != 0.0) && (f != -0.0);

    })), FragDef({Bytes}, Bool, void_fn_ptr([](BytesObject* b) -> bool {
      bool ret = b->count != 0;
      delete_reference(b);
      return ret;

    })), FragDef({Unicode}, Bool, void_fn_ptr([](UnicodeObject* u) -> bool {
      bool ret = u->count != 0;
      delete_reference(u);
      return ret;

    })), FragDef({List_Any}, Int, void_fn_ptr([](ListObject* l) -> bool {
      bool ret = l->count != 0;
      delete_reference(l);
      return ret;
    }))}, false},

    // Unicode input(Unicode='')
    {"input", {Unicode_Blank}, Unicode, void_fn_ptr([](UnicodeObject* prompt) -> UnicodeObject* {
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
      return unicode_new(data.data(), data.size());
    }), false},

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
    {"bool", {FragDef({Bool_False}, Bool, void_fn_ptr([](bool b) -> bool {
      return b;

    })), FragDef({Int}, Bool, void_fn_ptr([](int64_t i) -> bool {
      return static_cast<bool>(i);

    })), FragDef({Float}, Bool, void_fn_ptr([](double f) -> bool {
      return (f != 0.0) && (f != -0.0);

    })), FragDef({Bytes}, Bool, void_fn_ptr([](BytesObject* b) -> bool {
      bool ret = b->count != 0;
      delete_reference(b);
      return ret;

    })), FragDef({Unicode}, Bool, void_fn_ptr([](UnicodeObject* u) -> bool {
      bool ret = u->count != 0;
      delete_reference(u);
      return ret;

    })), FragDef({List_Any}, Int, void_fn_ptr([](ListObject* l) -> bool {
      bool ret = l->count != 0;
      delete_reference(l);
      return ret;
    }))}, false},

    // Int int(Int=0, Int=0)
    // Int int(Bytes, Int=0)
    // Int int(Unicode, Int=0)
    // Int int(Float, Int=0)
    {"int", {FragDef({Int_Zero, Int_Zero}, Int, void_fn_ptr([](
        int64_t i, int64_t, ExceptionBlock*) -> int64_t {
      return i;

    })), FragDef({Bytes, Int_Zero}, Int, void_fn_ptr([](
        BytesObject* s, int64_t base, ExceptionBlock* exc_block) -> int64_t {
      char* endptr;
      int64_t ret = strtoll(reinterpret_cast<const char*>(s->data), &endptr, base);
      delete_reference(s);

      if (endptr != s->data + s->count) {
        raise_python_exception_with_message(exc_block, global->ValueError_class_id,
            "invalid value for int()");
      }
      return ret;

    })), FragDef({Unicode, Int_Zero}, Int, void_fn_ptr([](
        UnicodeObject* s, int64_t base, ExceptionBlock* exc_block) -> int64_t {
      wchar_t* endptr;
      int64_t ret = wcstoll(s->data, &endptr, base);
      delete_reference(s);

      if (endptr != s->data + s->count) {
        raise_python_exception_with_message(exc_block, global->ValueError_class_id,
            "invalid value for int()");
      }
      return ret;

    })), FragDef({Float, Int_Zero}, Int, void_fn_ptr([](
        double x, int64_t, ExceptionBlock*) -> int64_t {
      return static_cast<int64_t>(x);
    }))}, true},

    // Float float(Float=0.0)
    // Float float(Int)
    // Float float(Bytes)
    // Float float(Unicode)
    {"float", {FragDef({Float_Zero}, Float, void_fn_ptr([](
        double f, ExceptionBlock*) -> double {
      return f;

    })), FragDef({Int}, Float, void_fn_ptr([](
        int64_t i, ExceptionBlock*) -> double {
      return static_cast<double>(i);

    })), FragDef({Bytes}, Float, void_fn_ptr([](
        BytesObject* s, ExceptionBlock* exc_block) -> double {
      char* endptr;
      double ret = strtod(s->data, &endptr);
      delete_reference(s);

      if (endptr != s->data + s->count) {
        raise_python_exception_with_message(exc_block, global->ValueError_class_id,
            "invalid value for float()");
      }
      return ret;

    })), FragDef({Unicode}, Float, void_fn_ptr([](
        UnicodeObject* s, ExceptionBlock* exc_block) -> double {
      wchar_t* endptr;
      double ret = wcstod(s->data, &endptr);
      delete_reference(s);

      if (endptr != s->data + s->count) {
        raise_python_exception_with_message(exc_block, global->ValueError_class_id,
            "invalid value for float()");
      }
      return ret;
    }))}, true},

    // Unicode repr(None)
    // Unicode repr(Bool)
    // Unicode repr(Int)
    // Unicode repr(Float)
    // Unicode repr(Bytes)
    // Unicode repr(Unicode)
    {"repr", {FragDef({None}, Unicode, void_fn_ptr([](void*) -> UnicodeObject* {
      static UnicodeObject* ret = unicode_new(L"None", 4);
      add_reference(ret);
      return ret;

    })), FragDef({Bool}, Unicode, void_fn_ptr([](bool v) -> UnicodeObject* {
      static UnicodeObject* true_str = unicode_new(L"True", 4);
      static UnicodeObject* false_str = unicode_new(L"False", 5);
      UnicodeObject* ret = v ? true_str : false_str;
      add_reference(ret);
      return ret;

    })), FragDef({Int}, Unicode, void_fn_ptr([](int64_t v) -> UnicodeObject* {
      wchar_t buf[24];
      return unicode_new(buf, swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%" PRId64, v));

    })), FragDef({Float}, Unicode, void_fn_ptr([](double v) -> UnicodeObject* {
      wchar_t buf[60]; // TODO: figure out how long this actually needs to be
      size_t count = swprintf(buf, sizeof(buf) / sizeof(buf[0]) - 2, L"%lg", v);

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

      return unicode_new(buf, wcslen(buf));

    })), FragDef({Bytes}, Unicode, void_fn_ptr([](BytesObject* v) -> UnicodeObject* {
      string escape_ret = escape(reinterpret_cast<const char*>(v->data), v->count);
      UnicodeObject* ret = unicode_new(NULL, escape_ret.size() + 3);
      ret->data[0] = L'b';
      ret->data[1] = L'\'';
      for (size_t x = 0; x < escape_ret.size(); x++) {
        ret->data[x + 2] = escape_ret[x];
      }
      ret->data[escape_ret.size() + 2] = L'\'';
      ret->data[escape_ret.size() + 3] = 0;
      delete_reference(v);
      return ret;

    })), FragDef({Unicode}, Unicode, void_fn_ptr([](UnicodeObject* v) -> UnicodeObject* {
      string escape_ret = escape(v->data, v->count);
      UnicodeObject* ret = unicode_new(NULL, escape_ret.size() + 2);
      ret->data[0] = L'\'';
      for (size_t x = 0; x < escape_ret.size(); x++) {
        ret->data[x + 1] = escape_ret[x];
      }
      ret->data[escape_ret.size() + 1] = L'\'';
      ret->data[escape_ret.size() + 2] = 0;
      delete_reference(v);
      return ret;
    }))}, false},

    // Int len(Bytes)
    // Int len(Unicode)
    // Int len(List[Any])
    // Int len(Tuple[...]) // unimplemented
    // Int len(Set[Any]) // unimplemented
    // Int len(Dict[Any, Any]) // unimplemented
    {"len", {FragDef({Bytes}, Int, void_fn_ptr([](BytesObject* s) -> int64_t {
      int64_t ret = s->count;
      delete_reference(s);
      return ret;
    })), FragDef({Unicode}, Int, void_fn_ptr([](UnicodeObject* s) -> int64_t {
      int64_t ret = s->count;
      delete_reference(s);
      return ret;
    })), FragDef({List_Any}, Int, void_fn_ptr([](ListObject* l) -> int64_t {
      int64_t ret = l->count;
      delete_reference(l);
      return ret;
    }))}, false},

    // Int abs(Int)
    // Float abs(Float)
    // Float abs(Complex) // unimplemented
    {"abs", {FragDef({Int}, Int, void_fn_ptr([](int64_t i) -> int64_t {
      return (i < 0) ? -i : i;
    })), FragDef({Float}, Float, void_fn_ptr([](double d) -> double {
      return (d < 0) ? -d : d;
    }))}, false},

    // Unicode chr(Int)
    {"chr", {Int}, Unicode, void_fn_ptr([](int64_t i, ExceptionBlock* exc_block) -> UnicodeObject* {
      if (i >= 0x110000) {
        raise_python_exception_with_message(exc_block, global->ValueError_class_id,
            "invalid value for chr()");
      }

      UnicodeObject* s = unicode_new(NULL, 1);
      s->data[0] = i;
      s->data[1] = 0;
      return s;
    }), true},

    // Int ord(Bytes) // apparently this isn't part of the Python standard anymore
    // Int ord(Unicode)
    {"ord", {FragDef({Bytes}, Int, void_fn_ptr([](BytesObject* s, ExceptionBlock* exc_block) -> int64_t {
      if (s->count != 1) {
        raise_python_exception_with_message(exc_block, global->ValueError_class_id,
            "string contains more than one character");
      }

      int64_t ret = (s->count < 1) ? -1 : s->data[0];
      delete_reference(s);
      return ret;

    })), FragDef({Unicode}, Int, void_fn_ptr([](UnicodeObject* s, ExceptionBlock* exc_block) -> int64_t {
      if (s->count != 1) {
        raise_python_exception_with_message(exc_block, global->ValueError_class_id,
            "string contains more than one character");
      }

      int64_t ret = (s->count < 1) ? -1 : s->data[0];
      delete_reference(s);
      return ret;
    }))}, true},

    // Unicode bin(Int)
    {"bin", {Int}, Unicode, void_fn_ptr([](int64_t i) -> UnicodeObject* {
      if (!i) {
        return unicode_new(L"0b0", 3);
      }

      UnicodeObject* s = unicode_new(NULL, 67);
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
    }), false},

    // Unicode oct(Int)
    {"oct", {Int}, Unicode, void_fn_ptr([](int64_t i) -> UnicodeObject* {
      if (!i) {
        return unicode_new(L"0o0", 3);
      }

      // 1<<63 is its own negative, so special-case it here so we can assume
      // below that the sign bit is never set
      if (i == -i) {
        return unicode_new(L"-0o1000000000000000000000", 25);
      }

      UnicodeObject* s = unicode_new(NULL, 25);
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
    }), false},

    // Unicode hex(Int)
    {"hex", {Int}, Unicode, void_fn_ptr([](int64_t i) -> UnicodeObject* {
      UnicodeObject* s = unicode_new(NULL, 19);
      s->count = swprintf(s->data, 19, L"%s0x%x", (i < 0) ? "-" : "", (i < 0) ? -i : i);
      return s;
    }), false},
  });

  static auto one_field_constructor = void_fn_ptr([](uint8_t* o, int64_t value) -> void* {
      // no need to deal with references; the reference passed to this function
      // becomes owned by the instance object
      *reinterpret_cast<int64_t*>(o + sizeof(InstanceObject)) = value;
      return o;
    });
  static auto one_field_reference_destructor = void_fn_ptr([](uint8_t* o) {
      delete_reference(*reinterpret_cast<void**>(o + sizeof(InstanceObject)));
      delete_reference(o);
    });
  static auto trivial_destructor = void_fn_ptr(&free);

  auto declare_trivial_exception = +[](const char* name) -> BuiltinClassDefinition {
    return BuiltinClassDefinition(name, {}, {}, void_fn_ptr(&free));
  };

  auto declare_message_exception = +[](const char* name) -> BuiltinClassDefinition {
    return BuiltinClassDefinition(name, {{"message", Unicode}}, {
          {"__init__", {Self, Unicode_Blank}, Self, one_field_constructor, false},
        }, one_field_reference_destructor);
  };

  vector<BuiltinClassDefinition> class_defs({
    declare_message_exception("NemesysCompilerError"),

    // TODO: probably all of these should have some attributes
    declare_message_exception("ArithmeticError"),
    declare_message_exception("AssertionError"),
    declare_message_exception("AttributeError"),
    declare_message_exception("BaseException"),
    declare_message_exception("BlockingIOError"),
    declare_message_exception("BrokenPipeError"),
    declare_message_exception("BufferError"),
    declare_message_exception("ChildProcessError"),
    declare_message_exception("ConnectionAbortedError"),
    declare_message_exception("ConnectionError"),
    declare_message_exception("ConnectionRefusedError"),
    declare_message_exception("ConnectionResetError"),
    declare_message_exception("EnvironmentError"),
    declare_message_exception("EOFError"),
    declare_message_exception("Exception"),
    declare_message_exception("FileExistsError"),
    declare_message_exception("FileNotFoundError"),
    declare_message_exception("FloatingPointError"),
    declare_message_exception("GeneratorExit"),
    declare_message_exception("IndexError"),
    declare_message_exception("InterruptedError"),
    declare_message_exception("IOError"),
    declare_message_exception("IsADirectoryError"),
    declare_message_exception("KeyboardInterrupt"),
    declare_message_exception("KeyError"),
    declare_message_exception("LookupError"),
    declare_trivial_exception("MemoryError"),
    declare_message_exception("ModuleNotFoundError"),
    declare_message_exception("NotADirectoryError"),
    declare_message_exception("NotImplementedError"),
    declare_message_exception("OverflowError"),
    declare_message_exception("PermissionError"),
    declare_message_exception("ProcessLookupError"),
    declare_message_exception("RecursionError"),
    declare_message_exception("ReferenceError"),
    declare_message_exception("ResourceWarning"),
    declare_message_exception("RuntimeError"),
    declare_message_exception("StopAsyncIteration"),
    declare_message_exception("StopIteration"),
    declare_message_exception("SystemError"),
    declare_message_exception("SystemExit"),
    declare_message_exception("TimeoutError"),
    declare_message_exception("TypeError"),
    declare_message_exception("UnicodeDecodeError"),
    declare_message_exception("UnicodeEncodeError"),
    declare_message_exception("UnicodeError"),
    declare_message_exception("UnicodeTranslateError"),
    declare_message_exception("ValueError"),
    declare_message_exception("ZeroDivisionError"),
    {"OSError", {{"errno", Int}}, {
        {"__init__", {Self, Int}, Self, one_field_constructor, false},
      }, trivial_destructor},

    {"bytes", {}, {
      /* TODO: implement these
      {"capitalize", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"center", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"count", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"decode", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"endswith", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"expandtabs", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"find", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"fromhex", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"hex", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"index", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isalnum", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isalpha", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isdigit", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"islower", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isspace", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"istitle", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isupper", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"join", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"ljust", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"lower", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"lstrip", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"maketrans", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"partition", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"replace", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rfind", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rindex", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rjust", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rpartition", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rsplit", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rstrip", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"split", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"splitlines", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"startswith", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"strip", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"swapcase", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"title", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"translate", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"upper", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"zfill", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      */
    }, trivial_destructor},

    {"unicode", {}, {
      /* TODO: implement these
      {"capitalize", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"casefold", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"center", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"count", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"encode", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"endswith", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"expandtabs", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"find", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"format", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"format_map", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"index", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isalnum", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isalpha", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isdecimal", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isdigit", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isidentifier", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"islower", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isnumeric", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isprintable", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isspace", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"istitle", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"isupper", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"join", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"ljust", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"lower", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"lstrip", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"maketrans", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"partition", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"replace", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rfind", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rindex", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rjust", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rpartition", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rsplit", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"rstrip", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"split", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"splitlines", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"startswith", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"strip", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"swapcase", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"title", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"translate", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"upper", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      {"zfill", {Self, TODO}, TODO, void_fn_ptr(NULL), false},
      */
    }, trivial_destructor},

    {"list", {}, {
      {"clear", {List_Any}, None, void_fn_ptr(&list_clear), false},
      {"append", {List_Same, Extension0}, None, void_fn_ptr(&list_append), true},
      {"insert", {List_Same, Int, Extension0}, None, void_fn_ptr(&list_insert), true},
      {"pop", {List_Same, Int_NegOne}, Extension0, void_fn_ptr(&list_pop), true},

      /* TODO: implement these
      {"copy", {Self}, List_Same, void_fn_ptr(), true},
      {"count", {Self, Extension0}, Int, void_fn_ptr(), true},
      {"extend", {Self, TODO}, None, void_fn_ptr(), true},
      {"index", {Self, Extension0}, Int, void_fn_ptr(), true},
      {"remove", {Self, Extension0}, None, void_fn_ptr(), true},
      {"reverse", {Self}, None, void_fn_ptr(), true},
      {"sort", {Self, key=TODO, Bool_False}, None, void_fn_ptr(), true},
      */
    }, void_fn_ptr(&list_delete)},

    {"tuple", {}, {
      /* TODO: implement these
      {"count", {Self, Extension0}, Int, void_fn_ptr(), true},
      {"index", {Self, Extension0}, Int, void_fn_ptr(), true},
      */
    }, NULL},

    {"set", {}, {
      /* TODO: implement these
      {"add", {Self, Extension0}, None, void_fn_ptr(), true},
      {"clear", {Self}, None, void_fn_ptr(), true},
      {"copy", {Self}, Set_Same, void_fn_ptr(), true},

      // TODO: these should support variadic arguments
      {"difference", {Self, Set_Same}, Set_Same, void_fn_ptr(), true},
      {"difference_update", {Self, Set_Same}, None, void_fn_ptr(), true},
      {"intersection", {Self, Set_Same}, Set_Same, void_fn_ptr(), true},
      {"intersection_update", {Self, Set_Same}, None, void_fn_ptr(), true},
      {"symmetric_difference", {Self, Set_Same}, Set_Same, void_fn_ptr(), true},
      {"symmetric_difference_update", {Self, Set_Same}, None, void_fn_ptr(), true},
      {"union", {Self, Set_Same}, Set_Same, void_fn_ptr(), true},
      {"update", {Self, Set_Same}, None, void_fn_ptr(), true},

      {"discard", {Self, Extension0}, None, void_fn_ptr(), true},
      {"remove", {Self, Extension0}, None, void_fn_ptr(), true},

      {"isdisjoint", {Self, Set_Same}, Bool, void_fn_ptr(), true},
      {"issubset", {Self, Set_Same}, Bool, void_fn_ptr(), true},
      {"issuperset", {Self, Set_Same}, Bool, void_fn_ptr(), true},
      {"pop", {Self}, Extension0, void_fn_ptr(), true},
      */
    }, NULL},

    {"dict", {}, {
      /* TODO: implement these
      {"clear", {Self}, None, void_fn_ptr(), true},
      {"copy", {Self}, Dict_Same, void_fn_ptr(), true},

      // TODO: this should support all the other weird forms (e.g. kwargs)
      {"update", {Self, Dict_Same}, None, void_fn_ptr(), true},

      // TODO: these should support not passing default
      {"get", {Self, Extension0, Extension1}, Extension1, void_fn_ptr(), true},
      {"pop", {Self, Extension0, Extension1}, Extension1, void_fn_ptr(), true},
      {"setdefault", {Self, Extension0, Extension1}, TODO, void_fn_ptr(), true},

      {"popitem", {Self}, Value(ValueType::Tuple, {Extension0, Extension1}), void_fn_ptr(), true},

      // TODO: these need view object types
      {"keys", {Self, TODO}, TODO, void_fn_ptr(), true},
      {"values", {Self, TODO}, TODO, void_fn_ptr(), true},
      {"items", {Self, TODO}, TODO, void_fn_ptr(), true},

      // TODO: this is a classmethod
      {"fromkeys", {Self, TODO}, TODO, void_fn_ptr(), true},
      */
    }, void_fn_ptr(&dictionary_delete)},
  });

  shared_ptr<ModuleContext> module(new ModuleContext(global_context, "builtins", globals));
  for (auto& def : function_defs) {
    module->create_builtin_function(def);
  }
  for (auto& def : class_defs) {
    module->create_builtin_class(def);
  }

  // TODO: this is kind of dumb; we shouldn't have to look them up in the map
  auto get_class_id = [&](const char* name) -> int64_t {
    const auto& var = module->global_variables.at(name);
    if (var.value.type != ValueType::Class) {
      throw logic_error(string_printf("required built-in class %s is not a class", name));
    }
    if (!var.value.value_known) {
      throw logic_error(string_printf("required built-in class %s has unknown value", name));
    }
    return var.value.class_id;
  };

  // populate global static symbols with useful exception class ids
  global_context->IndexError_class_id = get_class_id("IndexError");
  global_context->KeyError_class_id = get_class_id("KeyError");
  global_context->TypeError_class_id = get_class_id("TypeError");
  global_context->ValueError_class_id = get_class_id("ValueError");
  global_context->AssertionError_class_id = get_class_id("AssertionError");
  global_context->OSError_class_id = get_class_id("OSError");
  global_context->NemesysCompilerError_class_id = get_class_id("NemesysCompilerError");

  global_context->BytesObject_class_id = get_class_id("bytes");
  global_context->UnicodeObject_class_id = get_class_id("unicode");
  global_context->ListObject_class_id = get_class_id("list");
  global_context->TupleObject_class_id = get_class_id("tuple");
  global_context->DictObject_class_id = get_class_id("dict");
  global_context->SetObject_class_id = get_class_id("set");

  // create some common exception singletons. note that the MemoryError instance
  // probably can't be allocated when it's really needed, so instead it's a
  // global preallocated singleton
  MemoryError_instance.basic.refcount = 1;
  MemoryError_instance.basic.destructor = NULL;
  MemoryError_instance.class_id = get_class_id("MemoryError");

  return module;
}
