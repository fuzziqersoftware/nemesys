#include "Format.hh"

#include <inttypes.h>
#include <stdlib.h>

#include <phosg/Strings.hh>

#include "../Compiler/BuiltinFunctions.hh"
#include "../Compiler/Exception.hh"
#include "Strings.hh"

using namespace std;



extern shared_ptr<GlobalContext> global;

/* format opcodes look like this:
 *
 * %[[-]width][.max_chars]s - string
 *     - means padding comes after
 * %[+| ][[0|-]width]d - int
 *     0 means padding is zeroes, not spaces
 *     + means write + before number if it's positive
 *     space means write space before number if it's positive
 * %[+| ][[0|-]width][.precision]f - float
 *     0 means padding is zeroes, not spaces
 *     + means write + before number if it's positive
 *     space means write space before number if it's positive
 *     precision is the number of digits after the decimal point
 * %r - repr()
 * %a - ascii()
 *
 * width, max_chars, and precision may be parameterized by specifying *; in this
 * case expect an int argument preceding the value
 */

struct FormatSpecifier {
  bool alternate_form;
  bool zero_fill;
  bool left_justify;
  char sign_prefix; // either 0 (none), ' ', or '+'
  ssize_t width; // 0 means no length limit
  bool variable_width;
  ssize_t precision; // -1 means no precision specified
  bool variable_precision;
  char format_code;

  size_t offset;
  size_t length;

  FormatSpecifier() : alternate_form(false), zero_fill(false),
      left_justify(false), sign_prefix(0), width(0), variable_width(false),
      precision(-1), variable_precision(false), format_code(0), offset(0),
      length(0) { }

  string str(bool include_format = true, bool debug = false) const {
    string ret = "%";
    if (this->alternate_form) {
      ret += '#';
    }
    if (this->zero_fill) {
      ret += '0';
    }
    if (this->left_justify) {
      ret += '-';
    }
    if (this->sign_prefix) {
      ret += this->sign_prefix;
    }
    if (this->variable_width) {
      ret += '*';
    } else if (this->width) {
      ret += string_printf("%zd", this->width);
    }
    if (this->variable_precision) {
      ret += ".*";
    } else if (this->precision >= 0) {
      ret += string_printf(".%zd", this->width);
    }
    if (include_format) {
      ret += this->format_code;
    }
    if (debug) {
      ret += string_printf("(offset=%zd,length=%zd)", this->offset, this->length);
    }
    return ret;
  }

  wstring wstr(bool include_format = true, bool debug = false) const {
    wstring ret = L"%";
    if (this->alternate_form) {
      ret += L'#';
    }
    if (this->zero_fill) {
      ret += L'0';
    }
    if (this->left_justify) {
      ret += L'-';
    }
    if (this->sign_prefix) {
      ret += static_cast<wchar_t>(this->sign_prefix);
    }
    if (this->variable_width) {
      ret += L'*';
    } else if (this->width) {
      ret += wstring_printf(L"%zd", this->width);
    }
    if (this->variable_precision) {
      ret += L".*";
    } else if (this->precision >= 0) {
      ret += wstring_printf(L".%zd", this->width);
    }
    if (include_format) {
      ret += static_cast<wchar_t>(this->format_code);
    }
    if (debug) {
      ret += wstring_printf(L"(offset=%zd,length=%zd)", this->offset, this->length);
    }
    return ret;
  }
};

enum class FormatParserState {
  PrefixChars = 0,
  Width,
  Precision,
  FormatCode,
};

template <typename T>
static vector<struct FormatSpecifier> extract_formats(T* format, size_t count) {
  vector<struct FormatSpecifier> specs;
  struct FormatSpecifier* current = NULL;

  FormatParserState state = FormatParserState::PrefixChars;

  for (size_t x = 0; x < count;) {
    if (current == NULL) {
      if (format[x] == '%') {
        specs.emplace_back();
        current = &specs.back();
        current->offset = x;
      }
      x++;

    } else if (state == FormatParserState::PrefixChars) {
      if (format[x] == '+') {
        current->sign_prefix = '+';
        x++;
      } else if (format[x] == ' ') {
        current->sign_prefix = ' ';
        x++;
      } else if (format[x] == '0') {
        current->zero_fill = true;
        x++;
      } else if (format[x] == '-') {
        current->left_justify = true;
        x++;
      } else if (format[x] == '#') {
        current->alternate_form = true;
        x++;
      } else if (format[x] == '.') {
        state = FormatParserState::Precision;
      } else {
        state = FormatParserState::Width;
      }

    } else if (state == FormatParserState::Width) {
      if (format[x] == '*') {
        current->variable_width = true;
        x++;
      } else if (isdigit(format[x])) {
        current->width = current->width * 10 + (format[x] - '0');
        x++;
      } else if (format[x] == '.') {
        state = FormatParserState::Precision;
      } else {
        state = FormatParserState::FormatCode;
      }

    } else if (state == FormatParserState::Precision) {
      if (format[x] == '*') {
        current->variable_precision = true;
        x++;
      } else if (isdigit(format[x])) {
        if (current->precision < 0) {
          current->precision = 0;
        }
        current->precision = current->precision * 10 + (format[x] - '0');
        x++;
      } else {
        state = FormatParserState::FormatCode;
      }

    } else if (state == FormatParserState::FormatCode) {
      if ((format[x] == 'd') || (format[x] == 'i') || (format[x] == 'o') ||
          (format[x] == 'u') || (format[x] == 'x') || (format[x] == 'X') ||
          (format[x] == 'e') || (format[x] == 'E') || (format[x] == 'f') ||
          (format[x] == 'F') || (format[x] == 'g') || (format[x] == 'G') ||
          (format[x] == 'c') || (format[x] == 's') || (format[x] == '%')) {
        current->format_code = format[x];
        x++;
        current->length = x - current->offset;
        current = NULL;
        state = FormatParserState::PrefixChars;
      } else if ((format[x] == 'h') || (format[x] == 'l') || (format[x] == 'L')) {
        x++;
      } else {
        throw invalid_argument(string_printf("invalid format code: %c", format[x]));
      }

    } else {
      throw logic_error("invalid parser state");
    }
  }

  if (current) {
    throw invalid_argument("incomplete format specifier");
  }

  return specs;
}

void typecheck_format(const vector<FormatSpecifier>& specs, const vector<Value>& types) {
  size_t input_index = 0;
  for (const auto& spec : specs) {
    if (spec.variable_width) {
      if (input_index >= types.size()) {
        throw invalid_argument("not enough arguments");
      }
      if (types[input_index].type != ValueType::Int) {
        throw invalid_argument("variable-width argument is not an Int");
      }
      input_index++;
    }
    if (spec.variable_precision) {
      if (input_index >= types.size()) {
        throw invalid_argument("not enough arguments");
      }
      if (types[input_index].type != ValueType::Int) {
        throw invalid_argument("variable-precision argument is not an Int");
      }
      input_index++;
    }

    // % doesn't take an argument
    if (spec.format_code == '%') {
      continue;
    }

    if (input_index >= types.size()) {
      throw invalid_argument("not enough arguments");
    }
    ValueType input_type = types[input_index].type;

    // s accepts Unicode only
    // TODO: in python, s accepts any type that can be __str__()'d
    switch (spec.format_code) {
      case 's':
        if (input_type != ValueType::Unicode) {
          string type_str = types[input_index].str();
          throw invalid_argument(string_printf("incorrect type (%s) for %%%c",
              type_str.c_str(), spec.format_code));
        }
        break;

      case 'e': case 'E':
      case 'f': case 'F':
      case 'g': case 'G':
        if (input_type != ValueType::Float) {
          string type_str = types[input_index].str();
          throw invalid_argument(string_printf("incorrect type (%s) for %%%c",
              type_str.c_str(), spec.format_code));
        }
        break;

      case 'd': case 'i': case 'u': // also accepts Float in python; we only accept Int
      case 'c': // also accepts Unicode in python; we only accept Int
      case 'b':
      case 'o':
      case 'x': case 'X':
        if ((input_type != ValueType::Int) && (input_type != ValueType::Bool)) {
          string type_str = types[input_index].str();
          throw invalid_argument(string_printf("incorrect type (%s) for %%%c",
              type_str.c_str(), spec.format_code));
        }
        break;

      default:
        throw invalid_argument(string_printf("unknown format code %%%c",
            spec.format_code));
    }
    input_index++;
  }

  if (input_index != types.size()) {
    throw invalid_argument(string_printf("too many arguments (have %zu, expected %zu)",
        input_index, types.size()));
  }
}



void bytes_typecheck_format(const string& format, const vector<Value>& types) {
  auto specs = extract_formats(format.data(), format.size());
  typecheck_format(specs, types);
}

void unicode_typecheck_format(const wstring& format, const vector<Value>& types) {
  auto specs = extract_formats(format.data(), format.size());
  typecheck_format(specs, types);
}



void execute_format_spec(string& output, struct FormatSpecifier spec,
    const TupleObject* args, size_t& input_index) {
  if (spec.format_code == '%') {
    output += '%';
    return;
  }

  if (spec.variable_width) {
    spec.width = reinterpret_cast<int64_t>(tuple_get_item(args, input_index));
    spec.variable_width = false;
    input_index++;
  }
  if (spec.variable_precision) {
    spec.precision = reinterpret_cast<int64_t>(tuple_get_item(args, input_index));
    spec.variable_precision = false;
    input_index++;
  }
  int64_t x = reinterpret_cast<int64_t>(tuple_get_item(args, input_index));
  input_index++;

  if (spec.format_code == 's') {
    const BytesObject* s = reinterpret_cast<const BytesObject*>(x);
    // TODO: implement width and precision here
    output.append(s->data, s->count);

  } else if ((spec.format_code == 'd') || (spec.format_code == 'i') ||
      (spec.format_code == 'u') || (spec.format_code == 'o') ||
      (spec.format_code == 'X') || (spec.format_code == 'x') ||
      (spec.format_code == 'c')) {
    string format = (spec.str(false) + "ll") + spec.format_code;
    output += string_printf(format.c_str(), x);

  } else if ((spec.format_code == 'e') || (spec.format_code == 'E') ||
      (spec.format_code == 'f') || (spec.format_code == 'F') ||
      (spec.format_code == 'g') || (spec.format_code == 'G')) {
    double f = *reinterpret_cast<double*>(&x);
    string format = spec.str(false) + spec.format_code;
    output += string_printf(format.c_str(), f);
  }
}

// TODO: deduplicate this code with the above function
void execute_format_spec(wstring& output, struct FormatSpecifier spec,
    const TupleObject* args, size_t& input_index) {
  if (spec.format_code == '%') {
    output += L'%';
    return;
  }

  if (spec.variable_width) {
    spec.width = reinterpret_cast<int64_t>(tuple_get_item(args, input_index));
    spec.variable_width = false;
    input_index++;
  }
  if (spec.variable_precision) {
    spec.precision = reinterpret_cast<int64_t>(tuple_get_item(args, input_index));
    spec.variable_precision = false;
    input_index++;
  }
  int64_t x = reinterpret_cast<int64_t>(tuple_get_item(args, input_index));
  input_index++;

  if (spec.format_code == 's') {
    const UnicodeObject* s = reinterpret_cast<const UnicodeObject*>(x);
    // TODO: implement width and precision here
    output.append(s->data, s->count);

  } else if ((spec.format_code == 'd') || (spec.format_code == 'i') ||
      (spec.format_code == 'u') || (spec.format_code == 'o') ||
      (spec.format_code == 'X') || (spec.format_code == 'x') ||
      (spec.format_code == 'c')) {
    wstring format = (spec.wstr(false) + L"ll") + static_cast<wchar_t>(spec.format_code);

    wchar_t buf[64]; // this is plenty of space, right?
    if (spec.width > static_cast<ssize_t>(sizeof(buf) / sizeof(buf[0]))) {
      throw invalid_argument("formatted Ints may not be longer than 64 characters");
    }
    int len = swprintf(buf, sizeof(buf) / sizeof(buf[0]), format.c_str(), x);
    if ((len < 0) || (len >= static_cast<ssize_t>(sizeof(buf) / sizeof(buf[0])))) {
      throw invalid_argument("formatted Int is too long");
    }
    output.append(buf, len);

  } else if ((spec.format_code == 'e') || (spec.format_code == 'E') ||
      (spec.format_code == 'f') || (spec.format_code == 'F') ||
      (spec.format_code == 'g') || (spec.format_code == 'G')) {
    double f = *reinterpret_cast<double*>(&x);
    wstring format = spec.wstr(false) + static_cast<wchar_t>(spec.format_code);

    wchar_t buf[64]; // this is plenty of space, right?
    if (spec.width > static_cast<ssize_t>(sizeof(buf) / sizeof(buf[0]))) {
      throw invalid_argument("formatted Floats may not be longer than 64 characters");
    }
    int len = swprintf(buf, sizeof(buf) / sizeof(buf[0]), format.c_str(), f);
    if ((len < 0) || (len >= static_cast<ssize_t>(sizeof(buf) / sizeof(buf[0])))) {
      throw invalid_argument("formatted Float is too long");
    }
    output.append(buf, len);
  }
}

// TODO: this is a stupid template; make it require fewer arguments
template <typename ObjectType, typename StringType,
    ObjectType* (*string_new)(const StringType&)>
ObjectType* string_format(ObjectType* format, TupleObject* args,
    ExceptionBlock* exc_block, bool delete_tuple_reference = false) {
  ObjectType* ret = NULL;
  try {
    auto specs = extract_formats(format->data, format->count);
    size_t spec_index = 0;
    size_t input_index = 0;
    size_t format_index = 0;
    StringType output;

    while (format_index < format->count) {
      if (format->data[format_index] == '%') {
        auto& spec = specs[spec_index];
        execute_format_spec(output, spec, args, input_index);
        format_index += spec.length;
        spec_index++;
      } else {
        output += format->data[format_index];
        format_index++;
      }
    }

    ret = string_new(output);

  } catch (const exception& e) {
    if (delete_tuple_reference) {
      delete_reference(args);
    }
    raise_python_exception_with_message(exc_block, global->TypeError_class_id, e.what());
    throw;
  }

  if (delete_tuple_reference) {
    delete_reference(args);
  }
  return ret;
}

BytesObject* bytes_format(BytesObject* format, TupleObject* args,
    ExceptionBlock* exc_block) {
  return string_format<BytesObject, string, bytes_from_cxx_string>(
      format, args, exc_block);
}

UnicodeObject* unicode_format(UnicodeObject* format, TupleObject* args,
    ExceptionBlock* exc_block) {
  return string_format<UnicodeObject, wstring, unicode_from_cxx_wstring>(
      format, args, exc_block);
}

BytesObject* bytes_format_one(BytesObject* format, void* arg, bool is_object,
    ExceptionBlock* exc_block) {
  TupleObject* t = tuple_new(1, exc_block);
  tuple_set_item(t, 0, arg, is_object, exc_block);
  return string_format<BytesObject, string, bytes_from_cxx_string>(
      format, t, exc_block, true);
}

UnicodeObject* unicode_format_one(UnicodeObject* format, void* arg, bool is_object,
    ExceptionBlock* exc_block) {
  TupleObject* t = tuple_new(1, exc_block);
  tuple_set_item(t, 0, arg, is_object, exc_block);
  return string_format<UnicodeObject, wstring, unicode_from_cxx_wstring>(
      format, t, exc_block, true);
}
