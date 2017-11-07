#include "BuiltinFunctions.hh"

#include <inttypes.h>

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <memory>
#include <phosg/Strings.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Analysis.hh"
#include "Types/Strings.hh"
#include "Types/Dictionary.hh"
#include "Types/List.hh"
#include "Types/Instance.hh"
#include "Parser/PythonLexer.hh" // for escape()

// builtin module implementations
#include "Modules/__nemesys__.hh"
#include "Modules/errno.hh"
#include "Modules/math.hh"
#include "Modules/posix.hh"
#include "Modules/sys.hh"
#include "Modules/time.hh"

using namespace std;
using FragDef = BuiltinFragmentDefinition;



unordered_map<int64_t, FunctionContext> builtin_function_definitions;
unordered_map<int64_t, ClassContext> builtin_class_definitions;
unordered_map<string, Variable> builtin_names;

InstanceObject MemoryError_instance;
int64_t AssertionError_class_id = 0;
int64_t IndexError_class_id = 0;
int64_t KeyError_class_id = 0;
int64_t OSError_class_id = 0;
int64_t TypeError_class_id = 0;
int64_t ValueError_class_id = 0;

int64_t BytesObject_class_id = 0;
int64_t UnicodeObject_class_id = 0;
int64_t DictObject_class_id = 0;
int64_t ListObject_class_id = 0;
int64_t TupleObject_class_id = 0;
int64_t SetObject_class_id = 0;



static BytesObject* empty_bytes = bytes_new(NULL, NULL, 0);
static UnicodeObject* empty_unicode = unicode_new(NULL, NULL, 0);
static const Variable None(ValueType::None);
static const Variable Bool(ValueType::Bool);
static const Variable Bool_True(ValueType::Bool, true);
static const Variable Bool_False(ValueType::Bool, false);
static const Variable Int(ValueType::Int);
static const Variable Int_Zero(ValueType::Int, 0LL);
static const Variable Int_NegOne(ValueType::Int, -1LL);
static const Variable Float(ValueType::Float);
static const Variable Float_Zero(ValueType::Float, 0.0);
static const Variable Bytes(ValueType::Bytes);
static const Variable Unicode(ValueType::Unicode);
static const Variable Unicode_Blank(ValueType::Unicode, L"");
static const Variable Extension0(ValueType::ExtensionTypeReference, 0LL);
static const Variable Extension1(ValueType::ExtensionTypeReference, 1LL);
static const Variable Self(ValueType::Instance, 0LL, nullptr);
static const Variable List_Any(ValueType::List, vector<Variable>({Variable()}));
static const Variable List_Same(ValueType::List, vector<Variable>({Extension0}));
static const Variable Set_Any(ValueType::Set, vector<Variable>({Variable()}));
static const Variable Set_Same(ValueType::Set, vector<Variable>({Extension0}));
static const Variable Dict_Any(ValueType::Dict, vector<Variable>({Variable(), Variable()}));
static const Variable Dict_Same(ValueType::Dict, vector<Variable>({Extension0, Extension1}));



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
    create_builtin_name(def.name, Variable(ValueType::Function, function_id));
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
  static const unordered_map<string, unordered_set<Variable>> name_to_self_types({
    {"bytes", {Bytes}},
    {"unicode", {Unicode}},
    {"list", {List_Any, List_Same}},
    //{"tuple", ???}, // TODO: extension type refs won't work here
    {"set", {Set_Any, Set_Same}},
    {"dict", {Dict_Any, Dict_Same}},
  });
  unordered_set<Variable> self_types({{ValueType::Instance, 0LL, NULL}});
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
        if (frag_def.return_type != Variable(ValueType::Instance, 0LL, NULL)) {
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
    create_builtin_name(def.name, Variable(ValueType::Class, class_id));
  }

  return class_id;
}

void create_builtin_name(const char* name, const Variable& value) {
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
      return unicode_new(NULL, data.data(), data.size());
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
        raise_python_exception(exc_block, create_instance(ValueError_class_id));
      }
      return ret;

    })), FragDef({Unicode, Int_Zero}, Int, void_fn_ptr([](
        UnicodeObject* s, int64_t base, ExceptionBlock* exc_block) -> int64_t {
      wchar_t* endptr;
      int64_t ret = wcstoll(s->data, &endptr, base);
      delete_reference(s);

      if (endptr != s->data + s->count) {
        raise_python_exception(exc_block, create_instance(ValueError_class_id));
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
        raise_python_exception(exc_block, create_instance(ValueError_class_id));
      }
      return ret;

    })), FragDef({Unicode}, Float, void_fn_ptr([](
        UnicodeObject* s, ExceptionBlock* exc_block) -> double {
      wchar_t* endptr;
      double ret = wcstod(s->data, &endptr);
      delete_reference(s);

      if (endptr != s->data + s->count) {
        raise_python_exception(exc_block, create_instance(ValueError_class_id));
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
      static UnicodeObject* ret = unicode_new(NULL, L"None", 4);
      add_reference(ret);
      return ret;

    })), FragDef({Bool}, Unicode, void_fn_ptr([](bool v) -> UnicodeObject* {
      static UnicodeObject* true_str = unicode_new(NULL, L"True", 4);
      static UnicodeObject* false_str = unicode_new(NULL, L"False", 5);
      UnicodeObject* ret = v ? true_str : false_str;
      add_reference(ret);
      return ret;

    })), FragDef({Int}, Unicode, void_fn_ptr([](int64_t v) -> UnicodeObject* {
      wchar_t buf[24];
      return unicode_new(NULL, buf, swprintf(buf, sizeof(buf) / sizeof(buf[0]), L"%" PRId64, v));

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

      return unicode_new(NULL, buf, wcslen(buf));

    })), FragDef({Bytes}, Unicode, void_fn_ptr([](BytesObject* v) -> UnicodeObject* {
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

    })), FragDef({Unicode}, Unicode, void_fn_ptr([](UnicodeObject* v) -> UnicodeObject* {
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
        raise_python_exception(exc_block, create_instance(ValueError_class_id));
      }

      UnicodeObject* s = unicode_new(NULL, NULL, 1);
      s->data[0] = i;
      s->data[1] = 0;
      return s;
    }), true, true},

    // Int ord(Bytes) // apparently this isn't part of the Python standard anymore
    // Int ord(Unicode)
    {"ord", {FragDef({Bytes}, Int, void_fn_ptr([](BytesObject* s, ExceptionBlock* exc_block) -> int64_t {
      if (s->count != 1) {
        raise_python_exception(exc_block, create_instance(TypeError_class_id));
      }

      int64_t ret = (s->count < 1) ? -1 : s->data[0];
      delete_reference(s);
      return ret;

    })), FragDef({Unicode}, Int, void_fn_ptr([](UnicodeObject* s, ExceptionBlock* exc_block) -> int64_t {
      if (s->count != 1) {
        raise_python_exception(exc_block, create_instance(TypeError_class_id));
      }

      int64_t ret = (s->count < 1) ? -1 : s->data[0];
      delete_reference(s);
      return ret;
    }))}, true, true},

    // Unicode bin(Int)
    {"bin", {Int}, Unicode, void_fn_ptr([](int64_t i) -> UnicodeObject* {
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
    }), false, true},

    // Unicode oct(Int)
    {"oct", {Int}, Unicode, void_fn_ptr([](int64_t i) -> UnicodeObject* {
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
    }), false, true},

    // Unicode hex(Int)
    {"hex", {Int}, Unicode, void_fn_ptr([](int64_t i) -> UnicodeObject* {
      UnicodeObject* s = unicode_new(NULL, NULL, 19);
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
    // TODO: probably all of these should have some attributes
    declare_trivial_exception("ArithmeticError"),
    declare_message_exception("AssertionError"),
    declare_trivial_exception("AttributeError"),
    declare_trivial_exception("BaseException"),
    declare_trivial_exception("BlockingIOError"),
    declare_trivial_exception("BrokenPipeError"),
    declare_trivial_exception("BufferError"),
    declare_trivial_exception("ChildProcessError"),
    declare_trivial_exception("ConnectionAbortedError"),
    declare_trivial_exception("ConnectionError"),
    declare_trivial_exception("ConnectionRefusedError"),
    declare_trivial_exception("ConnectionResetError"),
    declare_trivial_exception("EnvironmentError"),
    declare_trivial_exception("EOFError"),
    declare_trivial_exception("Exception"),
    declare_trivial_exception("FileExistsError"),
    declare_trivial_exception("FileNotFoundError"),
    declare_trivial_exception("FloatingPointError"),
    declare_trivial_exception("GeneratorExit"),
    declare_trivial_exception("IndexError"),
    declare_trivial_exception("InterruptedError"),
    declare_trivial_exception("IOError"),
    declare_trivial_exception("IsADirectoryError"),
    declare_trivial_exception("KeyboardInterrupt"),
    declare_trivial_exception("KeyError"),
    declare_trivial_exception("LookupError"),
    declare_trivial_exception("MemoryError"),
    declare_trivial_exception("ModuleNotFoundError"),
    declare_trivial_exception("NotADirectoryError"),
    declare_trivial_exception("NotImplementedError"),
    declare_trivial_exception("OverflowError"),
    declare_trivial_exception("PermissionError"),
    declare_trivial_exception("ProcessLookupError"),
    declare_trivial_exception("RecursionError"),
    declare_trivial_exception("ReferenceError"),
    declare_trivial_exception("ResourceWarning"),
    declare_trivial_exception("RuntimeError"),
    declare_trivial_exception("StopAsyncIteration"),
    declare_trivial_exception("StopIteration"),
    declare_trivial_exception("SystemError"),
    declare_trivial_exception("SystemExit"),
    declare_trivial_exception("TimeoutError"),
    declare_trivial_exception("TypeError"),
    declare_trivial_exception("TypeError"),
    declare_trivial_exception("UnicodeDecodeError"),
    declare_trivial_exception("UnicodeEncodeError"),
    declare_trivial_exception("UnicodeError"),
    declare_trivial_exception("UnicodeTranslateError"),
    declare_trivial_exception("ValueError"),
    declare_trivial_exception("ValueError"),
    declare_trivial_exception("ZeroDivisionError"),
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
    }, void_fn_ptr(NULL), true},

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
    }, void_fn_ptr(NULL), true},

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

      {"popitem", {Self}, Variable(ValueType::Tuple, {Extension0, Extension1}), void_fn_ptr(), true, false},

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
  static const unordered_map<Variable, shared_ptr<Variable>> empty_dict_contents;
  static const Variable empty_dict(ValueType::Dict, empty_dict_contents);

  create_builtin_name("__annotations__", empty_dict);
  create_builtin_name("__build_class__", Variable(ValueType::Function));
  create_builtin_name("__debug__",       Variable(ValueType::Bool, true));
  create_builtin_name("__import__",      Variable(ValueType::Function));
  create_builtin_name("__loader__",      Variable(ValueType::None));
  create_builtin_name("__package__",     Variable(ValueType::None));
  create_builtin_name("__spec__",        Variable(ValueType::None));
  create_builtin_name("Ellipsis",        Variable());
  create_builtin_name("NotImplemented",  Variable());
  create_builtin_name("all",             Variable(ValueType::Function));
  create_builtin_name("any",             Variable(ValueType::Function));
  create_builtin_name("ascii",           Variable(ValueType::Function));
  create_builtin_name("bool",            Variable(ValueType::Function));
  create_builtin_name("bytearray",       Variable(ValueType::Function));
  create_builtin_name("bytes",           Variable(ValueType::Function));
  create_builtin_name("callable",        Variable(ValueType::Function));
  create_builtin_name("classmethod",     Variable(ValueType::Function));
  create_builtin_name("compile",         Variable(ValueType::Function));
  create_builtin_name("complex",         Variable(ValueType::Function));
  create_builtin_name("copyright",       Variable(ValueType::Function));
  create_builtin_name("credits",         Variable(ValueType::Function));
  create_builtin_name("delattr",         Variable(ValueType::Function));
  create_builtin_name("dir",             Variable(ValueType::Function));
  create_builtin_name("divmod",          Variable(ValueType::Function));
  create_builtin_name("enumerate",       Variable(ValueType::Function));
  create_builtin_name("eval",            Variable(ValueType::Function));
  create_builtin_name("exec",            Variable(ValueType::Function));
  create_builtin_name("exit",            Variable(ValueType::Function));
  create_builtin_name("filter",          Variable(ValueType::Function));
  create_builtin_name("format",          Variable(ValueType::Function));
  create_builtin_name("frozenset",       Variable(ValueType::Function));
  create_builtin_name("getattr",         Variable(ValueType::Function));
  create_builtin_name("globals",         Variable(ValueType::Function));
  create_builtin_name("hasattr",         Variable(ValueType::Function));
  create_builtin_name("hash",            Variable(ValueType::Function));
  create_builtin_name("help",            Variable(ValueType::Function));
  create_builtin_name("id",              Variable(ValueType::Function));
  create_builtin_name("isinstance",      Variable(ValueType::Function));
  create_builtin_name("issubclass",      Variable(ValueType::Function));
  create_builtin_name("iter",            Variable(ValueType::Function));
  create_builtin_name("license",         Variable(ValueType::Function));
  create_builtin_name("locals",          Variable(ValueType::Function));
  create_builtin_name("map",             Variable(ValueType::Function));
  create_builtin_name("max",             Variable(ValueType::Function));
  create_builtin_name("memoryview",      Variable(ValueType::Function));
  create_builtin_name("min",             Variable(ValueType::Function));
  create_builtin_name("next",            Variable(ValueType::Function));
  create_builtin_name("object",          Variable(ValueType::Function));
  create_builtin_name("open",            Variable(ValueType::Function));
  create_builtin_name("ord",             Variable(ValueType::Function));
  create_builtin_name("pow",             Variable(ValueType::Function));
  create_builtin_name("property",        Variable(ValueType::Function));
  create_builtin_name("quit",            Variable(ValueType::Function));
  create_builtin_name("range",           Variable(ValueType::Function));
  create_builtin_name("reversed",        Variable(ValueType::Function));
  create_builtin_name("round",           Variable(ValueType::Function));
  create_builtin_name("setattr",         Variable(ValueType::Function));
  create_builtin_name("slice",           Variable(ValueType::Function));
  create_builtin_name("sorted",          Variable(ValueType::Function));
  create_builtin_name("staticmethod",    Variable(ValueType::Function));
  create_builtin_name("str",             Variable(ValueType::Function));
  create_builtin_name("sum",             Variable(ValueType::Function));
  create_builtin_name("super",           Variable(ValueType::Function));
  create_builtin_name("type",            Variable(ValueType::Function));
  create_builtin_name("vars",            Variable(ValueType::Function));
  create_builtin_name("zip",             Variable(ValueType::Function));

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
  DECLARE_MODULE(errno),
  DECLARE_MODULE(math),
  DECLARE_MODULE(posix),
  DECLARE_MODULE(sys),
  DECLARE_MODULE(time),
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
