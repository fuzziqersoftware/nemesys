#include "BuiltinFunctions.hh"

#include <inttypes.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Contexts.hh"
#include "../Types/Strings.hh"
#include "../Types/Dictionary.hh"
#include "../Types/List.hh"
#include "../Types/Instance.hh"
#include "../AST/PythonLexer.hh" // for escape()

// builtin module implementations
#include "../Modules/__nemesys__.hh"
#include "../Modules/errno.hh"
#include "../Modules/math.hh"
#include "../Modules/posix.hh"
#include "../Modules/sys.hh"
#include "../Modules/time.hh"

using namespace std;
using FragDef = BuiltinFragmentDefinition;



unordered_map<int64_t, FunctionContext> builtin_function_definitions;
unordered_map<int64_t, ClassContext> builtin_class_definitions;
unordered_map<string, Value> builtin_names;

InstanceObject MemoryError_instance;
int64_t AssertionError_class_id = 0;
int64_t IndexError_class_id = 0;
int64_t KeyError_class_id = 0;
int64_t OSError_class_id = 0;
int64_t NemesysCompilerError_class_id = 0;
int64_t TypeError_class_id = 0;
int64_t ValueError_class_id = 0;

int64_t BytesObject_class_id = 0;
int64_t UnicodeObject_class_id = 0;
int64_t DictObject_class_id = 0;
int64_t ListObject_class_id = 0;
int64_t TupleObject_class_id = 0;
int64_t SetObject_class_id = 0;



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



static int64_t generate_function_id() {
  // all builtin functions and classes have negative IDs
  static int64_t next_function_id = -1;
  return next_function_id--;
}

int64_t create_builtin_function(BuiltinFunctionDefinition& def) {
  int64_t function_id = generate_function_id();

  builtin_function_definitions.emplace(piecewise_construct,
      forward_as_tuple(function_id), forward_as_tuple(nullptr, function_id,
        def.name, def.fragments, def.pass_exception_block));
  if (def.register_globally) {
    create_builtin_name(def.name, Value(ValueType::Function, function_id));
  }

  return function_id;
}

int64_t create_builtin_class(BuiltinClassDefinition& def) {
  int64_t class_id = generate_function_id();

  // create and register the class context
  // TODO: define a ClassContext constructor that will do all this
  ClassContext& cls = builtin_class_definitions.emplace(piecewise_construct,
      forward_as_tuple(class_id), forward_as_tuple(nullptr, class_id)).first->second;
  cls.destructor = def.destructor;
  cls.name = def.name;
  cls.ast_root = NULL;
  cls.attributes = def.attributes;
  cls.populate_dynamic_attributes();

  // note: we modify cls.attributes after this point, but only to add methods,
  // which doesn't affect the dynamic attribute set

  // built-in types like Bytes, Unicode, List, Tuple, Set, and Dict don't take
  // Instance as the first argument (instead they take their corresponding
  // built-in types), so allow those if the clss being defined is one of those
  static const unordered_map<string, unordered_set<Value>> name_to_self_types({
    {"bytes", {Bytes}},
    {"unicode", {Unicode}},
    {"list", {List_Any, List_Same}},
    //{"tuple", ???}, // TODO: extension type refs won't work here
    {"set", {Set_Any, Set_Same}},
    {"dict", {Dict_Any, Dict_Same}},
  });
  unordered_set<Value> self_types({{ValueType::Instance, static_cast<int64_t>(0), NULL}});
  try {
    self_types = name_to_self_types.at(def.name);
  } catch (const out_of_range&) { }

  // register the methods
  for (auto& method_def : def.methods) {
    // __del__ must not be given in the methods; it must already be compiled
    if (!strcmp(method_def.name, "__del__")) {
      throw logic_error(string_printf("%s defines __del__ in methods, not precompiled",
          def.name));
    }

    // patch all of the fragment definitions to include the correct class
    // instance as the first argument. they should already have an Instance
    // argument first, but with a missing class_id - the caller doesn't know the
    // class id when they call create_builtin_class
    for (auto& frag_def : method_def.fragments) {
      if (frag_def.arg_types.empty()) {
        throw logic_error(string_printf("%s.%s must take the class instance as an argument",
            def.name, method_def.name));
      }

      if (!self_types.count(frag_def.arg_types[0])) {
        string allowed_types_str;
        for (const auto& self_type : self_types) {
          if (!allowed_types_str.empty()) {
            allowed_types_str += ", ";
          }
          allowed_types_str += self_type.str();
        }
        string type_str = frag_def.arg_types[0].str();
        throw logic_error(string_printf("%s.%s cannot take %s as the first argument; one of [%s] is required",
            def.name, method_def.name, type_str.c_str(), allowed_types_str.c_str()));
      }
      if (frag_def.arg_types[0].type == ValueType::Instance) {
        frag_def.arg_types[0].class_id = class_id;
      }
    }

    // __init__ has some special behaviors
    int64_t function_id;
    if (!strcmp(method_def.name, "__init__")) {
      // if it's __init__, the return type must be the class instance, not None
      for (auto& frag_def : method_def.fragments) {
        if (frag_def.return_type != Value(ValueType::Instance, static_cast<int64_t>(0), NULL)) {
          throw logic_error(string_printf("%s.__init__ must return the class instance",
              def.name));
        }
        frag_def.return_type.class_id = class_id;
      }

      // __init__'s function id is the same as the class id
      function_id = class_id;

    } else {
      // all other functions have unique function_ids
      function_id = generate_function_id();
    }

    // register the function
    FunctionContext& fn = builtin_function_definitions.emplace(piecewise_construct,
        forward_as_tuple(function_id),
        forward_as_tuple(nullptr, function_id, method_def.name, method_def.fragments, method_def.pass_exception_block)).first->second;
    fn.class_id = class_id;

    // link the function as a class attribute
    if (!cls.attributes.emplace(piecewise_construct, forward_as_tuple(method_def.name),
        forward_as_tuple(ValueType::Function, function_id)).second) {
      throw logic_error(string_printf("%s.%s overrides a non-method attribute",
          def.name, method_def.name));
    }
  }

  // register the class name in the global scope if requested
  if (def.register_globally) {
    create_builtin_name(def.name, Value(ValueType::Class, class_id));
  }

  return class_id;
}

void create_builtin_name(const char* name, const Value& value) {
  builtin_names.emplace(name, value);
}



static void create_default_builtin_functions() {

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
    }))}, false, true},

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
    }))}, false, true},

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
    }), false, true},

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
    }))}, false, true},

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
        raise_python_exception_with_message(exc_block, ValueError_class_id,
            "invalid value for int()");
      }
      return ret;

    })), FragDef({Unicode, Int_Zero}, Int, void_fn_ptr([](
        UnicodeObject* s, int64_t base, ExceptionBlock* exc_block) -> int64_t {
      wchar_t* endptr;
      int64_t ret = wcstoll(s->data, &endptr, base);
      delete_reference(s);

      if (endptr != s->data + s->count) {
        raise_python_exception_with_message(exc_block, ValueError_class_id,
            "invalid value for int()");
      }
      return ret;

    })), FragDef({Float, Int_Zero}, Int, void_fn_ptr([](
        double x, int64_t, ExceptionBlock*) -> int64_t {
      return static_cast<int64_t>(x);
    }))}, true, true},

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
        raise_python_exception_with_message(exc_block, ValueError_class_id,
            "invalid value for float()");
      }
      return ret;

    })), FragDef({Unicode}, Float, void_fn_ptr([](
        UnicodeObject* s, ExceptionBlock* exc_block) -> double {
      wchar_t* endptr;
      double ret = wcstod(s->data, &endptr);
      delete_reference(s);

      if (endptr != s->data + s->count) {
        raise_python_exception_with_message(exc_block, ValueError_class_id,
            "invalid value for float()");
      }
      return ret;
    }))}, true, true},

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
    }))}, false, true},

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
    }))}, false, true},

    // Int abs(Int)
    // Float abs(Float)
    // Float abs(Complex) // unimplemented
    {"abs", {FragDef({Int}, Int, void_fn_ptr([](int64_t i) -> int64_t {
      return (i < 0) ? -i : i;
    })), FragDef({Float}, Float, void_fn_ptr([](double d) -> double {
      return (d < 0) ? -d : d;
    }))}, false, true},

    // Unicode chr(Int)
    {"chr", {Int}, Unicode, void_fn_ptr([](int64_t i, ExceptionBlock* exc_block) -> UnicodeObject* {
      if (i >= 0x110000) {
        raise_python_exception_with_message(exc_block, ValueError_class_id,
            "invalid value for chr()");
      }

      UnicodeObject* s = unicode_new(NULL, 1);
      s->data[0] = i;
      s->data[1] = 0;
      return s;
    }), true, true},

    // Int ord(Bytes) // apparently this isn't part of the Python standard anymore
    // Int ord(Unicode)
    {"ord", {FragDef({Bytes}, Int, void_fn_ptr([](BytesObject* s, ExceptionBlock* exc_block) -> int64_t {
      if (s->count != 1) {
        raise_python_exception_with_message(exc_block, ValueError_class_id,
            "string contains more than one character");
      }

      int64_t ret = (s->count < 1) ? -1 : s->data[0];
      delete_reference(s);
      return ret;

    })), FragDef({Unicode}, Int, void_fn_ptr([](UnicodeObject* s, ExceptionBlock* exc_block) -> int64_t {
      if (s->count != 1) {
        raise_python_exception_with_message(exc_block, ValueError_class_id,
            "string contains more than one character");
      }

      int64_t ret = (s->count < 1) ? -1 : s->data[0];
      delete_reference(s);
      return ret;
    }))}, true, true},

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
    }), false, true},

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
    }), false, true},

    // Unicode hex(Int)
    {"hex", {Int}, Unicode, void_fn_ptr([](int64_t i) -> UnicodeObject* {
      UnicodeObject* s = unicode_new(NULL, 19);
      s->count = swprintf(s->data, 19, L"%s0x%x", (i < 0) ? "-" : "", (i < 0) ? -i : i);
      return s;
    }), false, true},
  });

  // register everything
  for (auto& def : function_defs) {
    create_builtin_function(def);
  }
}

void create_default_builtin_classes() {
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
    return BuiltinClassDefinition(name, {}, {}, void_fn_ptr(&free), true);
  };

  auto declare_message_exception = +[](const char* name) -> BuiltinClassDefinition {
    return BuiltinClassDefinition(name, {{"message", Unicode}}, {
          {"__init__", {Self, Unicode_Blank}, Self, one_field_constructor, false, false},
        }, one_field_reference_destructor, true);
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
    declare_message_exception("TypeError"),
    declare_message_exception("UnicodeDecodeError"),
    declare_message_exception("UnicodeEncodeError"),
    declare_message_exception("UnicodeError"),
    declare_message_exception("UnicodeTranslateError"),
    declare_message_exception("ValueError"),
    declare_message_exception("ValueError"),
    declare_message_exception("ZeroDivisionError"),
    {"OSError", {{"errno", Int}}, {
        {"__init__", {Self, Int}, Self, one_field_constructor, false, false},
      }, trivial_destructor, true},

    {"bytes", {}, {
      /* TODO: implement these
      {"capitalize", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"center", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"count", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"decode", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"endswith", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"expandtabs", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"find", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"fromhex", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"hex", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"index", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isalnum", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isalpha", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isdigit", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"islower", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isspace", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"istitle", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isupper", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"join", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"ljust", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"lower", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"lstrip", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"maketrans", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"partition", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"replace", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rfind", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rindex", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rjust", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rpartition", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rsplit", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rstrip", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"split", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"splitlines", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"startswith", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"strip", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"swapcase", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"title", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"translate", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"upper", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"zfill", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      */
    }, void_fn_ptr(&list_delete), true},

    {"unicode", {}, {
      /* TODO: implement these
      {"capitalize", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"casefold", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"center", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"count", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"encode", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"endswith", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"expandtabs", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"find", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"format", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"format_map", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"index", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isalnum", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isalpha", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isdecimal", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isdigit", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isidentifier", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"islower", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isnumeric", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isprintable", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isspace", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"istitle", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"isupper", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"join", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"ljust", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"lower", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"lstrip", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"maketrans", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"partition", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"replace", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rfind", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rindex", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rjust", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rpartition", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rsplit", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"rstrip", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"split", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"splitlines", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"startswith", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"strip", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"swapcase", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"title", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"translate", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"upper", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      {"zfill", {Self, TODO}, TODO, void_fn_ptr(NULL), false, false},
      */
    }, void_fn_ptr(&list_delete), true},

    {"list", {}, {
      {"clear", {List_Any}, None, void_fn_ptr(&list_clear), false, false},
      {"append", {List_Same, Extension0}, None, void_fn_ptr(&list_append), true, false},
      {"insert", {List_Same, Int, Extension0}, None, void_fn_ptr(&list_insert), true, false},
      {"pop", {List_Same, Int_NegOne}, Extension0, void_fn_ptr(&list_pop), true, false},

      /* TODO: implement these
      {"copy", {Self}, List_Same, void_fn_ptr(), true, false},
      {"count", {Self, Extension0}, Int, void_fn_ptr(), true, false},
      {"extend", {Self, TODO}, None, void_fn_ptr(), true, false},
      {"index", {Self, Extension0}, Int, void_fn_ptr(), true, false},
      {"remove", {Self, Extension0}, None, void_fn_ptr(), true, false},
      {"reverse", {Self}, None, void_fn_ptr(), true, false},
      {"sort", {Self, key=TODO, Bool_False}, None, void_fn_ptr(), true, false},
      */
    }, void_fn_ptr(&list_delete), true},

    {"tuple", {}, {
      /* TODO: implement these
      {"count", {Self, Extension0}, Int, void_fn_ptr(), true, false},
      {"index", {Self, Extension0}, Int, void_fn_ptr(), true, false},
      */
    }, NULL, true},

    {"set", {}, {
      /* TODO: implement these
      {"add", {Self, Extension0}, None, void_fn_ptr(), true, false},
      {"clear", {Self}, None, void_fn_ptr(), true, false},
      {"copy", {Self}, Set_Same, void_fn_ptr(), true, false},

      // TODO: these should support variadic arguments
      {"difference", {Self, Set_Same}, Set_Same, void_fn_ptr(), true, false},
      {"difference_update", {Self, Set_Same}, None, void_fn_ptr(), true, false},
      {"intersection", {Self, Set_Same}, Set_Same, void_fn_ptr(), true, false},
      {"intersection_update", {Self, Set_Same}, None, void_fn_ptr(), true, false},
      {"symmetric_difference", {Self, Set_Same}, Set_Same, void_fn_ptr(), true, false},
      {"symmetric_difference_update", {Self, Set_Same}, None, void_fn_ptr(), true, false},
      {"union", {Self, Set_Same}, Set_Same, void_fn_ptr(), true, false},
      {"update", {Self, Set_Same}, None, void_fn_ptr(), true, false},

      {"discard", {Self, Extension0}, None, void_fn_ptr(), true, false},
      {"remove", {Self, Extension0}, None, void_fn_ptr(), true, false},

      {"isdisjoint", {Self, Set_Same}, Bool, void_fn_ptr(), true, false},
      {"issubset", {Self, Set_Same}, Bool, void_fn_ptr(), true, false},
      {"issuperset", {Self, Set_Same}, Bool, void_fn_ptr(), true, false},
      {"pop", {Self}, Extension0, void_fn_ptr(), true, false},
      */
    }, NULL, true},

    {"dict", {}, {
      /* TODO: implement these
      {"clear", {Self}, None, void_fn_ptr(), true, false},
      {"copy", {Self}, Dict_Same, void_fn_ptr(), true, false},

      // TODO: this should support all the other weird forms (e.g. kwargs)
      {"update", {Self, Dict_Same}, None, void_fn_ptr(), true, false},

      // TODO: these should support not passing default
      {"get", {Self, Extension0, Extension1}, Extension1, void_fn_ptr(), true, false},
      {"pop", {Self, Extension0, Extension1}, Extension1, void_fn_ptr(), true, false},
      {"setdefault", {Self, Extension0, Extension1}, TODO, void_fn_ptr(), true, false},

      {"popitem", {Self}, Value(ValueType::Tuple, {Extension0, Extension1}), void_fn_ptr(), true, false},

      // TODO: these need view object types
      {"keys", {Self, TODO}, TODO, void_fn_ptr(), true, false},
      {"values", {Self, TODO}, TODO, void_fn_ptr(), true, false},
      {"items", {Self, TODO}, TODO, void_fn_ptr(), true, false},

      // TODO: this is a classmethod
      {"fromkeys", {Self, TODO}, TODO, void_fn_ptr(), true, false},
      */
    }, void_fn_ptr(&dictionary_delete), true},
  });

  for (auto& def : class_defs) {
    create_builtin_class(def);
  }

  // populate global static symbols with useful exception class ids
  IndexError_class_id = builtin_names.at("IndexError").class_id;
  KeyError_class_id = builtin_names.at("KeyError").class_id;
  TypeError_class_id = builtin_names.at("TypeError").class_id;
  ValueError_class_id = builtin_names.at("ValueError").class_id;
  AssertionError_class_id = builtin_names.at("AssertionError").class_id;
  OSError_class_id = builtin_names.at("OSError").class_id;
  NemesysCompilerError_class_id = builtin_names.at("NemesysCompilerError").class_id;

  BytesObject_class_id = builtin_names.at("bytes").class_id;
  UnicodeObject_class_id = builtin_names.at("unicode").class_id;
  ListObject_class_id = builtin_names.at("list").class_id;
  TupleObject_class_id = builtin_names.at("tuple").class_id;
  DictObject_class_id = builtin_names.at("dict").class_id;
  SetObject_class_id = builtin_names.at("set").class_id;

  // create some common exception singletons. note that the MemoryError instance
  // probably can't be allocated when it's really needed, so instead it's a
  // global preallocated singleton
  MemoryError_instance.basic.refcount = 1;
  MemoryError_instance.basic.destructor = NULL;
  MemoryError_instance.class_id = builtin_names.at("MemoryError").class_id;
}

void create_default_builtin_names() {
  static const unordered_map<Value, shared_ptr<Value>> empty_dict_contents;
  static const Value empty_dict(ValueType::Dict, empty_dict_contents);

  create_builtin_name("__annotations__", empty_dict);
  create_builtin_name("__build_class__", Value(ValueType::Function));
  create_builtin_name("__debug__",       Value(ValueType::Bool, true));
  create_builtin_name("__import__",      Value(ValueType::Function));
  create_builtin_name("__loader__",      Value(ValueType::None));
  create_builtin_name("__package__",     Value(ValueType::None));
  create_builtin_name("__spec__",        Value(ValueType::None));
  create_builtin_name("Ellipsis",        Value());
  create_builtin_name("NotImplemented",  Value());
  create_builtin_name("all",             Value(ValueType::Function));
  create_builtin_name("any",             Value(ValueType::Function));
  create_builtin_name("ascii",           Value(ValueType::Function));
  create_builtin_name("bool",            Value(ValueType::Function));
  create_builtin_name("bytearray",       Value(ValueType::Function));
  create_builtin_name("bytes",           Value(ValueType::Function));
  create_builtin_name("callable",        Value(ValueType::Function));
  create_builtin_name("classmethod",     Value(ValueType::Function));
  create_builtin_name("compile",         Value(ValueType::Function));
  create_builtin_name("complex",         Value(ValueType::Function));
  create_builtin_name("copyright",       Value(ValueType::Function));
  create_builtin_name("credits",         Value(ValueType::Function));
  create_builtin_name("delattr",         Value(ValueType::Function));
  create_builtin_name("dir",             Value(ValueType::Function));
  create_builtin_name("divmod",          Value(ValueType::Function));
  create_builtin_name("enumerate",       Value(ValueType::Function));
  create_builtin_name("eval",            Value(ValueType::Function));
  create_builtin_name("exec",            Value(ValueType::Function));
  create_builtin_name("exit",            Value(ValueType::Function));
  create_builtin_name("filter",          Value(ValueType::Function));
  create_builtin_name("format",          Value(ValueType::Function));
  create_builtin_name("frozenset",       Value(ValueType::Function));
  create_builtin_name("getattr",         Value(ValueType::Function));
  create_builtin_name("globals",         Value(ValueType::Function));
  create_builtin_name("hasattr",         Value(ValueType::Function));
  create_builtin_name("hash",            Value(ValueType::Function));
  create_builtin_name("help",            Value(ValueType::Function));
  create_builtin_name("id",              Value(ValueType::Function));
  create_builtin_name("isinstance",      Value(ValueType::Function));
  create_builtin_name("issubclass",      Value(ValueType::Function));
  create_builtin_name("iter",            Value(ValueType::Function));
  create_builtin_name("license",         Value(ValueType::Function));
  create_builtin_name("locals",          Value(ValueType::Function));
  create_builtin_name("map",             Value(ValueType::Function));
  create_builtin_name("max",             Value(ValueType::Function));
  create_builtin_name("memoryview",      Value(ValueType::Function));
  create_builtin_name("min",             Value(ValueType::Function));
  create_builtin_name("next",            Value(ValueType::Function));
  create_builtin_name("object",          Value(ValueType::Function));
  create_builtin_name("open",            Value(ValueType::Function));
  create_builtin_name("ord",             Value(ValueType::Function));
  create_builtin_name("pow",             Value(ValueType::Function));
  create_builtin_name("property",        Value(ValueType::Function));
  create_builtin_name("quit",            Value(ValueType::Function));
  create_builtin_name("range",           Value(ValueType::Function));
  create_builtin_name("reversed",        Value(ValueType::Function));
  create_builtin_name("round",           Value(ValueType::Function));
  create_builtin_name("setattr",         Value(ValueType::Function));
  create_builtin_name("slice",           Value(ValueType::Function));
  create_builtin_name("sorted",          Value(ValueType::Function));
  create_builtin_name("staticmethod",    Value(ValueType::Function));
  create_builtin_name("str",             Value(ValueType::Function));
  create_builtin_name("sum",             Value(ValueType::Function));
  create_builtin_name("super",           Value(ValueType::Function));
  create_builtin_name("type",            Value(ValueType::Function));
  create_builtin_name("vars",            Value(ValueType::Function));
  create_builtin_name("zip",             Value(ValueType::Function));

  create_default_builtin_functions();
  create_default_builtin_classes();
}



struct BuiltinModule {
  bool initialized;
  void (*initialize)();

  // note: this has to be a function because the module pointers are statically
  // initialized, and there's no guarantee that builtin_modules isn't
  // initialized before they are
  shared_ptr<ModuleContext> (*get_module)();

  BuiltinModule(void (*initialize)(), shared_ptr<ModuleContext> (*get_module)())
      : initialized(false), initialize(initialize), get_module(get_module) { }
};

#define DECLARE_MODULE(name) {#name, {name##_initialize, []() -> shared_ptr<ModuleContext> {return name##_module;}}}
static unordered_map<string, BuiltinModule> builtin_modules({
  DECLARE_MODULE(__nemesys__),
  DECLARE_MODULE(errno),
  DECLARE_MODULE(math),
  DECLARE_MODULE(posix),
  DECLARE_MODULE(sys),
  DECLARE_MODULE(time),
});
#undef DECLARE_MODULE

shared_ptr<ModuleContext> get_builtin_module(const string& module_name) {
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
