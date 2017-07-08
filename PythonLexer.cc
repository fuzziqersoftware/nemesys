#include <inttypes.h>
#include <string.h>

#include <phosg/Strings.hh>
#include <string>
#include <unordered_set>

#include "PythonLexer.hh"

using namespace std;

using TokenType = PythonLexer::Token::TokenType;


string unescape_bytes(const char* s, size_t size) {
  string ret;
  for (size_t x = 0; x < size;) {
    if (s[x] == '\\') {
      if (x == size - 1) {
        throw invalid_argument("escape at end of string");
      }
      switch (s[x + 1]) {
        case '\n':
          x += 2;
          break;
        case '\\':
          ret += '\\';
          x += 2;
          break;
        case '\'':
          ret += '\'';
          x += 2;
          break;
        case '\"':
          ret += '\"';
          x += 2;
          break;
        case 'a':
          ret += '\a';
          x += 2;
          break;
        case 'b':
          ret += '\b';
          x += 2;
          break;
        case 'f':
          ret += '\f';
          x += 2;
          break;
        case 'n':
          ret += '\n';
          x += 2;
          break;
        case 'r':
          ret += '\r';
          x += 2;
          break;
        case 't':
          ret += '\t';
          x += 2;
          break;
        case 'v':
          ret += '\v';
          x += 2;
          break;
        case '0':
        case '1':
        case '2':
        case '3':
          if (x >= size - 3) {
            throw invalid_argument("incomplete octal escape sequence");
          }
          if (s[x + 2] > '7' || s[x + 2] < '0' || s[x + 3] > '7' || s[x + 3] < '0') {
            throw invalid_argument("invalid character in octal escape sequence");
          }
          ret += ((s[x + 1] - '0') << 6) | ((s[x + 2] - '0') << 3) | (s[x + 3] - '0');
          x += 4;
          break;
        case 'x':
          if (x >= size - 3) {
            throw invalid_argument("incomplete hex escape sequence");
          }
          // NB: this behavior differs from the CPython's - we only examine the
          // two characters after the \x sequence
          if (!isxdigit(s[x + 2]) || !isxdigit(s[x + 3])) {
            throw invalid_argument("invalid character in hex escape sequence");
          }
          ret += static_cast<char>((value_for_hex_char(s[x + 2]) << 4) | value_for_hex_char(s[x + 3]));
          x += 4;
          break;
        default:
          ret += '\\';
          x++;
      }
    } else {
      ret += s[x];
      x++;
    }
  }
  return ret;
}

string unescape_bytes(const string& s) {
  return unescape_bytes(s.data(), s.size());
}

wstring unescape_unicode(const char* s, size_t size) {
  wstring ret;
  for (size_t x = 0; x < size;) {
    if (s[x] == '\\') {
      if (x == size - 1) {
        throw invalid_argument("escape at end of string");
      }
      switch (s[x + 1]) {
        case '\n':
          x += 2;
          break;
        case '\\':
          ret += '\\';
          x += 2;
          break;
        case '\'':
          ret += '\'';
          x += 2;
          break;
        case '\"':
          ret += '\"';
          x += 2;
          break;
        case 'a':
          ret += '\a';
          x += 2;
          break;
        case 'b':
          ret += '\b';
          x += 2;
          break;
        case 'f':
          ret += '\f';
          x += 2;
          break;
        case 'n':
          ret += '\n';
          x += 2;
          break;
        case 'r':
          ret += '\r';
          x += 2;
          break;
        case 't':
          ret += '\t';
          x += 2;
          break;
        case 'v':
          ret += '\v';
          x += 2;
          break;
        case '0':
        case '1':
        case '2':
        case '3':
          if (x >= size - 3) {
            throw invalid_argument("incomplete octal escape sequence");
          }
          if (s[x + 2] > '7' || s[x + 2] < '0' || s[x + 3] > '7' || s[x + 3] < '0') {
            throw invalid_argument("invalid character in octal escape sequence");
          }
          ret += ((s[x + 1] - '0') << 6) | ((s[x + 2] - '0') << 3) | (s[x + 3] - '0');
          x += 4;
          break;
        case 'x':
          if (x >= size - 3) {
            throw invalid_argument("incomplete hex escape sequence");
          }
          // NB: this behavior differs from the CPython's - we only examine the
          // two characters after the \x sequence
          if (!isxdigit(s[x + 2]) || !isxdigit(s[x + 3])) {
            throw invalid_argument("invalid character in hex escape sequence");
          }
          ret += static_cast<wchar_t>((value_for_hex_char(s[x + 2]) << 4) | value_for_hex_char(s[x + 3]));
          x += 4;
          break;

        // the following cases are unique to unicode literals
        case 'N':
          throw invalid_argument("named character lookup is unsupported");

        case 'u':
          if (x >= size - 5) {
            throw invalid_argument("incomplete unicode16 escape sequence");
          }
          if (!isxdigit(s[x + 2]) || !isxdigit(s[x + 3]) ||
              !isxdigit(s[x + 4]) || !isxdigit(s[x + 5])) {
            throw invalid_argument("invalid character in unicode16 escape sequence");
          }
          ret += static_cast<char>((value_for_hex_char(s[x + 2]) << 12) |
                                   (value_for_hex_char(s[x + 3]) << 8) |
                                   (value_for_hex_char(s[x + 4]) << 4) |
                                   value_for_hex_char(s[x + 5]));
          x += 6;
          break;

        case 'U':
          if (x >= size - 9) {
            throw invalid_argument("incomplete unicode32 escape sequence");
          }
          if (!isxdigit(s[x + 2]) || !isxdigit(s[x + 3]) ||
              !isxdigit(s[x + 4]) || !isxdigit(s[x + 5]) ||
              !isxdigit(s[x + 6]) || !isxdigit(s[x + 7]) ||
              !isxdigit(s[x + 8]) || !isxdigit(s[x + 9])) {
            throw invalid_argument("invalid character in unicode32 escape sequence");
          }
          ret += static_cast<char>((value_for_hex_char(s[x + 2]) << 28) |
                                   (value_for_hex_char(s[x + 3]) << 24) |
                                   (value_for_hex_char(s[x + 4]) << 20) |
                                   (value_for_hex_char(s[x + 5]) << 16) |
                                   (value_for_hex_char(s[x + 6]) << 12) |
                                   (value_for_hex_char(s[x + 7]) << 8) |
                                   (value_for_hex_char(s[x + 8]) << 4) |
                                   value_for_hex_char(s[x + 9]));
          x += 10;
          break;

        default:
          ret += '\\';
          x++;
      }
    } else {
      ret += s[x];
      x++;
    }
  }
  return ret;
}

wstring unescape_unicode(const string& s) {
  return unescape_unicode(s.data(), s.size());
}

string escape(const char* s, size_t size) {
  string ret;
  for (size_t x = 0; x < size; x++) {
    char ch = s[x];
    if (ch == '\\') {
      ret += "\\\\";
    } else if (ch == '\'') {
      ret += "\\\'";
    } else if (ch == '\"') {
      ret += "\\\"";
    } else if (ch == '\a') {
      ret += "\\a";
    } else if (ch == '\b') {
      ret += "\\b";
    } else if (ch == '\f') {
      ret += "\\f";
    } else if (ch == '\n') {
      ret += "\\n";
    } else if (ch == '\r') {
      ret += "\\r";
    } else if (ch == '\t') {
      ret += "\\t";
    } else if (ch == '\v') {
      ret += "\\v";
    } else if (ch < 0x20 || ch > 0x7F) {
      ret += string_printf("\\x%02" PRIX8, static_cast<uint8_t>(ch));
    } else {
      ret += ch;
    }
  }
  return ret;
}

string escape(const string& s) {
  return escape(s.data(), s.size());
}

string escape(const wchar_t* s, size_t size) {
  string ret;
  for (size_t x = 0; x < size; x++) {
    char ch = s[x];
    if (ch == '\\') {
      ret += "\\\\";
    } else if (ch == '\'') {
      ret += "\\\'";
    } else if (ch == '\"') {
      ret += "\\\"";
    } else if (ch == '\a') {
      ret += "\\a";
    } else if (ch == '\b') {
      ret += "\\b";
    } else if (ch == '\f') {
      ret += "\\f";
    } else if (ch == '\n') {
      ret += "\\n";
    } else if (ch == '\r') {
      ret += "\\r";
    } else if (ch == '\t') {
      ret += "\\t";
    } else if (ch == '\v') {
      ret += "\\v";
    } else if (ch & 0xFFFF0000) {
      ret += string_printf("\\U%08" PRIX32, static_cast<uint32_t>(ch));
    } else if (ch & 0xFFFFFF00) {
      ret += string_printf("\\u%04" PRIX16, static_cast<uint16_t>(ch));
    } else if (ch < 0x20 || ch > 0x7F) {
      ret += string_printf("\\x%02" PRIX8, static_cast<uint8_t>(ch));
    } else {
      ret += ch;
    }
  }
  return ret;
}

string escape(const wstring& s) {
  return escape(s.data(), s.size());
}



static const vector<const char*> error_names = {
  "NoLexError",
  "UnmatchedParenthesis",
  "UnmatchedBrace",
  "UnmatchedBracket",
  "MisalignedUnindent",
  "BadToken",
  "UnterminatedString",
  "BadScientificNotation",
  "IncompleteLexing",
};

const char* PythonLexer::name_for_tokenization_error(TokenizationError type) {
  return error_names.at(static_cast<size_t>(type));
}



static const vector<const char*> token_names = {
  "_Dynamic",
  "_BytesConstant",
  "_UnicodeConstant",
  "_Integer",
  "_Float",
  "_Indent",
  "_Unindent",
  "_Comment",
  "Del",
  "Pass",
  "Break",
  "Continue",
  "Return",
  "Raise",
  "Import",
  "From",
  "As",
  "Def",
  "Global",
  "Nonlocal",
  "Exec",
  "Assert",
  "If",
  "Else",
  "Elif",
  "With",
  "While",
  "For",
  "In",
  "NotIn",
  "Not",
  "And",
  "Or",
  "Try",
  "Except",
  "Finally",
  "Lambda",
  "_Colon",
  "Class",
  "Yield",
  "_At",
  "_OpenParen",
  "_CloseParen",
  "_Newline",
  "_Equals",
  "_Comma",
  "_Asterisk",
  "_DoubleAsterisk",
  "_Semicolon",
  "_PlusEquals",
  "_MinusEquals",
  "_AsteriskEquals",
  "_SlashEquals",
  "_PercentEquals",
  "_AndEquals",
  "_OrEquals",
  "_XorEquals",
  "_LeftShiftEquals",
  "_RightShiftEquals",
  "_DoubleTimesEquals",
  "_DoubleSlashEquals",
  "_LeftShift",
  "_RightShift",
  "_Dot",
  "_Plus",
  "_Minus",
  "_Slash",
  "_LessThan",
  "_GreaterThan",
  "_Equality",
  "_GreaterOrEqual",
  "_LessOrEqual",
  "_NotEqual",
  "Is",
  "IsNot",
  "_Or",
  "_Xor",
  "_And",
  "_Percent",
  "_DoubleSlash",
  "_Tilde",
  "_OpenBracket",
  "_CloseBracket",
  "_OpenBrace",
  "_CloseBrace",
  "_Backtick",
  "_BackslashNewline",
  "_InvalidToken",
};

struct StaticTokenString {
  TokenType type;
  const char* text;
};

StaticTokenString wordy_static_tokens[] = {
  {TokenType::Del,                 "del"},
  {TokenType::Pass,                "pass"},
  {TokenType::Break,               "break"},
  {TokenType::Continue,            "continue"},
  {TokenType::Return,              "return"},
  {TokenType::Raise,               "raise"},
  {TokenType::Import,              "import"},
  {TokenType::From,                "from"},
  {TokenType::As,                  "as"},
  {TokenType::Def,                 "def"},
  {TokenType::Global,              "global"},
  {TokenType::Exec,                "exec"},
  {TokenType::Assert,              "assert"},
  {TokenType::If,                  "if"},
  {TokenType::Else,                "else"},
  {TokenType::Elif,                "elif"},
  {TokenType::With,                "with"},
  {TokenType::While,               "while"},
  {TokenType::For,                 "for"},
  {TokenType::In,                  "in"},
  {TokenType::Not,                 "not"},
  {TokenType::And,                 "and"},
  {TokenType::Or,                  "or"},
  {TokenType::Try,                 "try"},
  {TokenType::Except,              "except"},
  {TokenType::Finally,             "finally"},
  {TokenType::Lambda,              "lambda"},
  {TokenType::Class,               "class"},
  {TokenType::Yield,               "yield"},
  {TokenType::Is,                  "is"},
};

StaticTokenString symbolic_static_tokens[] = {
  {TokenType::_BackslashNewline,   "\\\r\n"},
  {TokenType::_BackslashNewline,   "\\\n"},
  {TokenType::_Newline,            "\r\n"}, // thanks, windows
  {TokenType::_LeftShiftEquals,    "<<="},
  {TokenType::_RightShiftEquals,   ">>="},
  {TokenType::_DoubleTimesEquals,  "**="},
  {TokenType::_DoubleSlashEquals,  "//="},
  {TokenType::_DoubleSlash,        "//"},
  {TokenType::_DoubleAsterisk,     "**"},
  {TokenType::_LeftShift,          "<<"},
  {TokenType::_RightShift,         ">>"},
  {TokenType::_Equality,           "=="},
  {TokenType::_GreaterOrEqual,     ">="},
  {TokenType::_LessOrEqual,        "<="},
  {TokenType::_NotEqual,           "!="},
  {TokenType::_NotEqual,           "<>"}, // lol, this is dumb syntax
  {TokenType::_PlusEquals,         "+="},
  {TokenType::_MinusEquals,        "-="},
  {TokenType::_AsteriskEquals,     "*="},
  {TokenType::_SlashEquals,        "/="},
  {TokenType::_PercentEquals,      "%="},
  {TokenType::_AndEquals,          "&="},
  {TokenType::_OrEquals,           "|="},
  {TokenType::_XorEquals,          "^="},
  {TokenType::_At,                 "@"},
  {TokenType::_OpenParen,          "("},
  {TokenType::_CloseParen,         ")"},
  {TokenType::_Newline,            "\n"},
  {TokenType::_Colon,              ":"},
  {TokenType::_LessThan,           "<"},
  {TokenType::_GreaterThan,        ">"},
  {TokenType::_Equals,             "="},
  {TokenType::_Comma,              ","},
  {TokenType::_Semicolon,          ";"},
  {TokenType::_Dot,                "."},
  {TokenType::_Plus,               "+"},
  {TokenType::_Minus,              "-"},
  {TokenType::_Asterisk,           "*"},
  {TokenType::_Slash,              "/"},
  {TokenType::_Or,                 "|"},
  {TokenType::_Xor,                "^"},
  {TokenType::_And,                "&"},
  {TokenType::_Percent,            "%"},
  {TokenType::_Tilde,              "~"},
  {TokenType::_OpenBracket,        "["},
  {TokenType::_CloseBracket,       "]"},
  {TokenType::_OpenBrace,          "{"},
  {TokenType::_CloseBrace,         "}"},
  {TokenType::_Backtick,           "`"},
};

bool PythonLexer::Token::is_open_bracket_token(TokenType type) {
  return (type == TokenType::_OpenParen ||
          type == TokenType::_OpenBrace ||
          type == TokenType::_OpenBracket ||
          type == TokenType::_Indent ||
          type == TokenType::Lambda);
}

bool PythonLexer::Token::is_close_bracket_token(TokenType type) {
  return (type == TokenType::_CloseParen ||
          type == TokenType::_CloseBrace ||
          type == TokenType::_CloseBracket ||
          type == TokenType::_Unindent ||
          type == TokenType::_Colon);
}

bool PythonLexer::Token::token_requires_opener(TokenType type) {
  return (type == TokenType::_CloseParen ||
          type == TokenType::_CloseBrace ||
          type == TokenType::_CloseBracket ||
          type == TokenType::_Unindent);
}

TokenType PythonLexer::Token::get_closing_bracket_token_type(TokenType type) {
  return static_cast<TokenType>(static_cast<int64_t>(type) + 1);
}

bool PythonLexer::Token::is_static_token(TokenType type) {
  if (type == TokenType::_Dynamic ||
      type == TokenType::_BytesConstant ||
      type == TokenType::_UnicodeConstant ||
      type == TokenType::_Integer ||
      type == TokenType::_Float ||
      type == TokenType::_Indent ||
      type == TokenType::_Unindent ||
      type == TokenType::_Comment) {
    return false;
  }
  return true;
}

bool PythonLexer::Token::is_operator_token(TokenType type) {
  static const unordered_set<TokenType> operator_tokens({
    TokenType::In, TokenType::NotIn, TokenType::Not, TokenType::And,
    TokenType::Or, TokenType::_Asterisk, TokenType::_DoubleAsterisk,
    TokenType::_LeftShift, TokenType::_RightShift, TokenType::_Dot,
    TokenType::_Plus, TokenType::_Minus, TokenType::_Slash,
    TokenType::_LessThan, TokenType::_GreaterThan, TokenType::_Equality,
    TokenType::_GreaterOrEqual, TokenType::_LessOrEqual, TokenType::_NotEqual,
    TokenType::Is, TokenType::IsNot, TokenType::_Or, TokenType::_Xor,
    TokenType::_And, TokenType::_Percent, TokenType::_DoubleSlash,
    TokenType::_Tilde});
  return (operator_tokens.count(type) > 0);
}

const char* PythonLexer::Token::name_for_token_type(TokenType type) {
  return token_names.at(static_cast<size_t>(type));
}




static bool is_dynamic_character(char c) {
  return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_');
}

static bool is_dynamic_first_character(char c) {
  return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '_');
}

static bool is_digit(char c) {
  return (c >= '0') && (c <= '9');
}

static bool is_hex_digit(char c) {
  return ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F')) || ((c >= '0') && (c <= '9'));
}

static size_t get_blank_line_length(const char* str) {
  // returns the length of the first line in str (including the \r\n or \n at
  // the end) if the line is blank. if the line is not blank, return 0.

  size_t indent = 0;
  while (str[indent] == ' ') {
    indent++;
  }
  if (str[indent] == '\r' && str[indent + 1] == '\n') {
    return indent + 2;
  }
  if (str[indent] == '\n') {
    return indent + 1;
  }
  return 0;
}

static size_t get_line_indent(const char* str) {
  size_t indent = 0;
  while (str[indent] == ' ') {
    indent++;
  }
  return indent;
}


PythonLexer::Token::Token(TokenType type, const string& string_data,
    float float_data, int64_t int_data, size_t text_offset, size_t text_length)
    : type(type), string_data(string_data), float_data(float_data),
    int_data(int_data), text_offset(text_offset), text_length(text_length) { }

string PythonLexer::Token::str() const {
  return string_printf("Token[%s, s=\'%s\', f=%g, i=%" PRId64 " off=%zu len=%zu]",
      this->name_for_token_type(this->type), this->string_data.c_str(),
      this->float_data, this->int_data, this->text_offset, this->text_length);
}



PythonLexer::tokenization_error::tokenization_error(TokenizationError error,
    size_t offset, size_t line) : runtime_error(string_printf(
      "lexing failed: %s at offset %zu (line %zu)",
      name_for_tokenization_error(error), offset, line)),
    error(error), offset(offset) { }



PythonLexer::PythonLexer(shared_ptr<const SourceFile> source) : source(source) {
  const auto& data = this->source->data();
  vector<size_t> indent_levels;
  vector<TokenType> paren_stack;
  indent_levels.push_back(0);
  size_t indent = 0;
  size_t position = 0;
  size_t len = data.size();
  const char* str = data.c_str();

  while (position < len) {

    if (str[0] == ' ') { // no whitespace plz
      position++;
      str++;
      continue;
    }

    // start checking for tokens
    // TODO: use unique_ptr here
    Token token(TokenType::_InvalidToken, string(), 0, 0, 0, 0);

    // check for comments
    if (str[0] == '#') {
      const char* match = str + 2;
      while ((match[-2] == '\\') || (match[-1] != '\n')) {
        match++;
      }
      size_t comment_len = (match - str) - 1;
      token = Token(TokenType::_Comment, string(str, comment_len), 0, 0, position, comment_len);
    }

    // check for prefixed docstrings (single or double quoted)
    if ((token.type == TokenType::_InvalidToken) && ((str[0] == 'b') || (str[0] == 'u') || (str[0] == 'B') || (str[0] == 'U')) &&
        (!strncmp(str + 1, "\'\'\'", 3) || !strncmp(str + 1, "\"\"\"", 3))) {
      size_t len = 4;
      // this condition handles cases like """ \""" """
      while (str[len + 3] && (strncmp(&str[1], &str[len], 3) || str[len - 1] == '\\')) {
        len++;
      }
      if (strncmp(&str[1], &str[len], 3)) {
        throw tokenization_error(TokenizationError::UnterminatedString,
            position, this->source->line_number_of_offset(position));
      }
      TokenType type = ((str[0] == 'b') || (str[0] == 'B')) ? TokenType::_BytesConstant : TokenType::_UnicodeConstant;
      token = Token(type, string(str + 4, len - 4), 0, 0, position, len + 3);
    }

    // check for docstrings (single or double quoted)
    if ((token.type == TokenType::_InvalidToken) && (!strncmp(str, "\'\'\'", 3) || !strncmp(str, "\"\"\"", 3))) {
      size_t len = 3;
      while (str[len + 2] && (strncmp(&str[0], &str[len], 3) || str[len - 1] == '\\')) {
        len++;
      }
      if (strncmp(&str[0], &str[len], 3)) {
        throw tokenization_error(TokenizationError::UnterminatedString, position,
            this->source->line_number_of_offset(position));
      }
      token = Token(TokenType::_UnicodeConstant, string(str + 3, len - 3),
          0, 0, position, len + 3);
    }

    // check for prefixed strings (single or double quoted)
    if ((token.type == TokenType::_InvalidToken) && ((str[0] == 'b') || (str[0] == 'u') || (str[0] == 'B') || (str[0] == 'U')) &&
        (str[1] == '\"' || str[1] == '\'')) {
      size_t len = 2;
      while (str[len] && (str[1] != str[len] || str[len - 1] == '\\')) {
        len++;
      }
      if (str[1] != str[len]) {
        throw tokenization_error(TokenizationError::UnterminatedString, position,
            this->source->line_number_of_offset(position));
      }
      TokenType type = ((str[0] == 'b') || (str[0] == 'B')) ? TokenType::_BytesConstant : TokenType::_UnicodeConstant;
      token = Token(type, string(str + 2, len - 2), 0, 0, position, len + 1);
    }

    // check for normal strings (single or double quoted)
    if ((token.type == TokenType::_InvalidToken) && (str[0] == '\"' || str[0] == '\'')) {
      size_t len = 1;
      while (str[len] && (str[0] != str[len] || str[len - 1] == '\\')) {
        len++;
      }
      if (str[0] != str[len]) {
        throw tokenization_error(TokenizationError::UnterminatedString, position,
            this->source->line_number_of_offset(position));
      }
      token = Token(TokenType::_UnicodeConstant, string(str + 1, len - 1),
          0, 0, position, len + 1);
    }

    // check for dynamic tokens
    if ((token.type == TokenType::_InvalidToken) && is_dynamic_first_character(str[0])) {
      size_t len = 1;
      while (is_dynamic_character(str[len])) {
        len++;
      }
      string token_str(str, len);

      // if it's a wordy static token, use that token type instead
      TokenType type = TokenType::_Dynamic;
      for (size_t x = 0; x < sizeof(wordy_static_tokens) / sizeof(wordy_static_tokens[0]); x++) {
        if (!strcmp(token_str.c_str(), wordy_static_tokens[x].text)) {
          type = wordy_static_tokens[x].type;
        }
      }
      token = Token(type,
          (type == TokenType::_Dynamic) ? token_str : "", 0, 0, position, len);
    }

    // check for integers / floats
    if (token.type == TokenType::_InvalidToken) {
      const char* match = str;
      size_t match_length = 0;
      bool float_match = false;
      bool hex_match = false;

      // check for floats with leading .: .[0-9]+([eE][+-]?[0-9]+)?
      if (match[0] == '.' && is_digit(match[1])) {
        match++;
        while (is_digit(match[0])) {
          match++;
        }
        if (match[0] == 'e' || match[0] == 'E') {
          match++;
          if (match[0] == '+' || match[0] == '-') {
            match++;
          }
          if (!is_digit(match[0])) {
            throw tokenization_error(TokenizationError::BadScientificNotation,
                position, this->source->line_number_of_offset(position));
          }
          while (is_digit(match[0])) {
            match++;
          }
        }
        match_length = match - str;
        float_match = true;
      }

      // check for floats: [0-9]+.[0-9]*([eE][+-]?[0-9]+)?
      // also checks for ints; if it doesn't have a . or an e[] then it's an int
      match = str;
      if (is_digit(match[0])) {
        match++;
        while (is_digit(match[0])) {
          match++;
        }
        if (match[0] == '.') {
          match++;
          while (is_digit(match[0])) {
            match++;
          }
          float_match = true;
        }
        if (match[0] == 'e' || match[0] == 'E') {
          match++;
          if (match[0] == '+' || match[0] == '-') {
            match++;
          }
          if (!is_digit(match[0])) {
            throw tokenization_error(TokenizationError::BadScientificNotation,
                position, this->source->line_number_of_offset(position));
          }
          while (is_digit(match[0])) {
            match++;
          }
          float_match = true;
        }
        if (!float_match && match[0] == 'L') {
          match++;
        }
        match_length = match - str;
      }

      // check for hexadecimal: 0x[0-9A-Fa-f]+
      match = str;
      if (match[0] == '0' && (match[1] == 'x' || match[1] == 'X') && is_hex_digit(match[2])) {
        match += 3;
        while (is_hex_digit(match[0])) {
          match++;
        }
        match_length = match - str;
        hex_match = true;
      }

      // if there was a match, parse out the number
      if (match_length && float_match) {
        double data;
        sscanf(str, "%lg", &data);
        token = Token(TokenType::_Float, string(str, match_length), data, 0,
            position, match_length);
      } else if (match_length && !float_match) {
        long long data;
        sscanf(str, hex_match ? "%llX" : "%lld", &data);
        token = Token(TokenType::_Integer, string(str, match_length), 0,
            data, position, match_length);
      }
    }

    // check for symbolic static tokens
    if (token.type == TokenType::_InvalidToken) {
      TokenType type = TokenType::_Dynamic;
      size_t len = 0;
      for (size_t x = 0; x < sizeof(symbolic_static_tokens) / sizeof(symbolic_static_tokens[0]); x++) {
        if (!strncmp(str, symbolic_static_tokens[x].text, strlen(symbolic_static_tokens[x].text))) {
          type = symbolic_static_tokens[x].type;
          len = strlen(symbolic_static_tokens[x].text);
          break;
        }
      }
      if (type != TokenType::_Dynamic) {
        token = Token(type, "", 0, 0, position, len);
      }
    }

    // if we haven't read a token, there's a syntax error
    // if we have read a token, advance past it
    if (token.type == TokenType::_InvalidToken) {
      throw tokenization_error(TokenizationError::BadToken, position,
          this->source->line_number_of_offset(position));
    } else {
      position += token.text_length;
      str += token.text_length;
    }

    // keep track of open parens/braces/brackets
    size_t next_indent;
    switch (token.type) {
      case TokenType::_OpenParen:
      case TokenType::_OpenBracket:
      case TokenType::_OpenBrace:
        paren_stack.push_back(token.type);
        this->tokens.emplace_back(move(token));
        break;

      // fail if a close paren/brace/bracket doesn't match
      case TokenType::_CloseParen:
      case TokenType::_CloseBracket:
      case TokenType::_CloseBrace: {
        TokenType expected_type = static_cast<TokenType>(static_cast<size_t>(token.type) - 1);
        if (paren_stack.size() < 1 || paren_stack.back() != expected_type) {
          throw tokenization_error(TokenizationError::UnmatchedParenthesis, position,
              this->source->line_number_of_offset(position));
        }
        paren_stack.pop_back();
        this->tokens.emplace_back(move(token));
        break;
      }

      // a newline might be followed by a nonzero number of indents/unindents,
      // but only if the paren_stack is empty
      case TokenType::_Newline:
        if (paren_stack.size() > 0) {
          break;
        }
        this->tokens.emplace_back(move(token));

        // skip any blank lines - we don't enforce indentation for them
        size_t blank_line_len;
        while ((blank_line_len = get_blank_line_length(str))) {
          position += blank_line_len;
          str += blank_line_len;
        }

        next_indent = get_line_indent(str);
        if (next_indent > indent_levels[indent]) {
          indent_levels.push_back(next_indent);
          indent++;
          this->tokens.emplace_back(TokenType::_Indent, "", 0, 0, position, 0);
        } else while (next_indent < indent_levels[indent]) {
          indent_levels.pop_back();
          indent--;
          this->tokens.emplace_back(TokenType::_Unindent, "", 0, 0, position, 0);
        }
        if (indent_levels[indent] != next_indent) {
          throw tokenization_error(TokenizationError::MisalignedUnindent, position,
              this->source->line_number_of_offset(position));
        }
        position += next_indent;
        str += next_indent;
        break;

      // throw out backslash-newlines
      case TokenType::_BackslashNewline:
        break;

      // everything else goes directly onto the result list
      default:
        this->tokens.emplace_back(move(token));
    }
  }

  // postprocessing steps

  // delete comments
  for (size_t x = 0; x < this->tokens.size(); x++) {
    if (this->tokens[x].type == TokenType::_Comment) {
      if (x == this->tokens.size() - 1) {
        this->tokens.pop_back(); // the last token is a comment; delete it
      } else {
        this->tokens.erase(this->tokens.begin() + x, this->tokens.begin() + x + 1);
        x--;
      }
    }
  }

  // remove leading newlines
  while ((this->tokens.size() > 0) && (this->tokens[0].type == TokenType::_Newline)) {
    this->tokens.erase(this->tokens.begin());
  }

  // replace composite tokens, duplicate newlines, and semicolons
  for (size_t x = 0; x < this->tokens.size() - 1; x++) {
    if (this->tokens[x].type == TokenType::Is && this->tokens[x + 1].type == TokenType::Not) {
      this->tokens.erase(this->tokens.begin() + x, this->tokens.begin() + x + 1);
      this->tokens[x] = Token(TokenType::IsNot, "", 0, 0, x, 0);
    } else if (this->tokens[x].type == TokenType::Not && this->tokens[x + 1].type == TokenType::In) {
      this->tokens.erase(this->tokens.begin() + x, this->tokens.begin() + x + 1);
      this->tokens[x] = Token(TokenType::NotIn, "", 0, 0, x, 0);
    } else if (this->tokens[x].type == TokenType::_Semicolon) {
      this->tokens[x].type = TokenType::_Newline;
      x--;
    } else if (this->tokens[x].type == TokenType::_Newline && this->tokens[x + 1].type == TokenType::_Newline) {
      this->tokens.erase(this->tokens.begin() + x, this->tokens.begin() + x + 1);
      x--;
    }
  }

  // make sure it ends with a newline
  if (this->tokens.size() == 0 || this->tokens.back().type != TokenType::_Newline) {
    this->tokens.emplace_back(TokenType::_Newline, "", 0, 0, this->tokens.size(), 0);
  }

  // close any indents that may be open
    while (indent_levels.back() > 0) {
    indent_levels.pop_back();
    this->tokens.emplace_back(TokenType::_Unindent, "", 0, 0, position, 0);
  }
}

shared_ptr<const SourceFile> PythonLexer::get_source() const {
  return this->source;
}

const vector<PythonLexer::Token>& PythonLexer::get_tokens() const {
  return this->tokens;
}
