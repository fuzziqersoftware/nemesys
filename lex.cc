#include <string.h>

#include <set>
#include <string>

#include "lex.hh"

static const char* token_names[] = {
  "_Dynamic",
  "_StringConstant",
  "_Integer",
  "_Float",
  "_Indent",
  "_Unindent",
  "_Comment",
  "Print",
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

static const char* error_names[] = {
  "NoLexError",
  "UnmatchedParenthesis",
  "UnmatchedBrace",
  "UnmatchedBracket",
  "MisalignedUnindent",
  "BadToken",
  "UnterminatedStringConstant",
  "BadScientificNotation",
  "IncompleteLexing",
};

struct StaticTokenString {
  TokenType type;
  const char* text;
};

StaticTokenString wordy_static_tokens[] = {
  {Print,               "print"},
  {Del,                 "del"},
  {Pass,                "pass"},
  {Break,               "break"},
  {Continue,            "continue"},
  {Return,              "return"},
  {Raise,               "raise"},
  {Import,              "import"},
  {From,                "from"},
  {As,                  "as"},
  {Def,                 "def"},
  {Global,              "global"},
  {Exec,                "exec"},
  {Assert,              "assert"},
  {If,                  "if"},
  {Else,                "else"},
  {Elif,                "elif"},
  {With,                "with"},
  {While,               "while"},
  {For,                 "for"},
  {In,                  "in"},
  {Not,                 "not"},
  {And,                 "and"},
  {Or,                  "or"},
  {Try,                 "try"},
  {Except,              "except"},
  {Finally,             "finally"},
  {Lambda,              "lambda"},
  {Class,               "class"},
  {Yield,               "yield"},
  {Is,                  "is"},
};

StaticTokenString symbolic_static_tokens[] = {
  {_BackslashNewline,   "\\\r\n"},
  {_BackslashNewline,   "\\\n"},
  {_Newline,            "\r\n"}, // thanks, windows
  {_LeftShiftEquals,    "<<="},
  {_RightShiftEquals,   ">>="},
  {_DoubleTimesEquals,  "**="},
  {_DoubleSlashEquals,  "//="},
  {_DoubleSlash,        "//"},
  {_DoubleAsterisk,     "**"},
  {_LeftShift,          "<<"},
  {_RightShift,         ">>"},
  {_Equality,           "=="},
  {_GreaterOrEqual,     ">="},
  {_LessOrEqual,        "<="},
  {_NotEqual,           "!="},
  {_NotEqual,           "<>"}, // lol, this is dumb syntax
  {_PlusEquals,         "+="},
  {_MinusEquals,        "-="},
  {_AsteriskEquals,     "*="},
  {_SlashEquals,        "/="},
  {_PercentEquals,      "%="},
  {_AndEquals,          "&="},
  {_OrEquals,           "|="},
  {_XorEquals,          "^="},
  {_At,                 "@"},
  {_OpenParen,          "("},
  {_CloseParen,         ")"},
  {_Newline,            "\n"},
  {_Colon,              ":"},
  {_LessThan,           "<"},
  {_GreaterThan,        ">"},
  {_Equals,             "="},
  {_Comma,              ","},
  {_Semicolon,          ";"},
  {_Dot,                "."},
  {_Plus,               "+"},
  {_Minus,              "-"},
  {_Asterisk,           "*"},
  {_Slash,              "/"},
  {_Or,                 "|"},
  {_Xor,                "^"},
  {_And,                "&"},
  {_Percent,            "%"},
  {_Tilde,              "~"},
  {_OpenBracket,        "["},
  {_CloseBracket,       "]"},
  {_OpenBrace,          "{"},
  {_CloseBrace,         "}"},
  {_Backtick,           "`"},
};

InputToken::InputToken(TokenType type, const string& string_data,
    float float_data, long long int_data, size_t text_offset, size_t text_length) :
  type(type), string_data(string_data), float_data(float_data),
  int_data(int_data), text_offset(text_offset), text_length(text_length) { }

InputToken::~InputToken() { }

bool is_open_bracket_token(TokenType type) {
  return (type == _OpenParen || type == _OpenBrace || type == _OpenBracket || type == _Indent || type == Lambda);
}

bool is_close_bracket_token(TokenType type) {
  return (type == _CloseParen || type == _CloseBrace || type == _CloseBracket || type == _Unindent || type == _Colon);
}

bool token_requires_opener(TokenType type) {
  return (type == _CloseParen || type == _CloseBrace || type == _CloseBracket || type == _Unindent);
}

TokenType get_closing_bracket_token_type(TokenType type) {
  return (TokenType)((int)type + 1);
}

bool is_static_token(TokenType type) {
  if (type == _Dynamic || type == _StringConstant || type == _Integer || type == _Float || type == _Indent || type == _Unindent || type == _Comment)
    return false;
  return true;
}

bool is_operator_token(TokenType type) {
  static TokenType operator_tokens_array[] = {
    In, NotIn, Not, And, Or, _Asterisk, _DoubleAsterisk, _LeftShift,
    _RightShift, _Dot, _Plus, _Minus, _Slash, _LessThan, _GreaterThan,
    _Equality, _GreaterOrEqual, _LessOrEqual, _NotEqual, Is, IsNot, _Or, _Xor,
    _And, _Percent, _DoubleSlash, _Tilde};
  static set<TokenType> operator_tokens(operator_tokens_array, operator_tokens_array + (sizeof(operator_tokens_array) / sizeof(operator_tokens_array[0])));

  return (operator_tokens.count(type) > 0);
}

const char* name_for_token_type(TokenType type) {
  unsigned int t = type;
  if (t > sizeof(token_names) / sizeof(token_names[0]))
    return NULL;
  return token_names[t];
}

const char* name_for_tokenization_error(TokenizationError type) {
  unsigned int t = type;
  if (t > sizeof(error_names) / sizeof(error_names[0]))
    return NULL;
  return error_names[t];
}

bool is_dynamic_character(char c) {
  return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_');
}

bool is_dynamic_first_character(char c) {
  return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '_');
}

bool is_digit(char c) {
  return (c >= '0') && (c <= '9');
}

bool is_hex_digit(char c) {
  return ((c >= 'a') && (c <= 'f')) || ((c >= 'A') && (c <= 'F')) || ((c >= '0') && (c <= '9'));
}

static int get_blank_line_length(const char* str) {
  // returns the length of the first line in str (including the \r\n or \n at
  // the end) if the line is blank. if the line is not blank, return 0.

  int indent = 0;
  while (str[indent] == ' ')
    indent++;
  if (str[indent] == '\r' && str[indent + 1] == '\n')
    return indent + 2;
  if (str[indent] == '\n')
    return indent + 1;
  return 0;
}

static int get_line_indent(const char* str) {
  int indent = 0;
  while (str[indent] == ' ')
    indent++;
  return indent;
}

void tokenize_string(const char* data, TokenStream* result) {
  // tokenizes the given string into a stream of InputToken objects.

  result->tokens.clear();
  result->error = NoLexError;
  result->failure_offset = -1;

  vector<int> indent_levels;
  vector<TokenType> paren_stack;
  indent_levels.push_back(0);
  int indent = 0;
  int position = 0;
  int len = strlen(data);
  const char* str = data;

  while (position < len) {

    if (str[0] == ' ') { // no whitespace plz
      position++;
      str++;
      continue;
    }

    // start checking for tokens
    InputToken* token = NULL;

    // check for comments
    if (str[0] == '#') {
      const char* match = str + 2;
      while (match[-2] == '\\' || match[-1] != '\n')
        match++;
      int comment_len = (match - str) - 1;
      token = new InputToken(_Comment, string(str, comment_len), 0, 0, position, comment_len);
    }

    // check for dynamic tokens
    if (!token && is_dynamic_first_character(str[0])) {
      int len = 1;
      while (is_dynamic_character(str[len]))
        len++;
      string token_str(str, len);

      // if it's a wordy static token, use that token type instead
      TokenType type = _Dynamic;
      for (size_t x = 0; x < sizeof(wordy_static_tokens) / sizeof(wordy_static_tokens[0]); x++)
        if (!strcmp(token_str.c_str(), wordy_static_tokens[x].text))
          type = wordy_static_tokens[x].type;
      token = new InputToken(type, (type == _Dynamic) ? token_str : "", 0, 0, position, len);
    }

    // check for docstrings (single or double quoted)
    if (!token && (!memcmp(str, "\'\'\'", 3) || !memcmp(str, "\"\"\"", 3))) {
      int len = 3;
      while (str[len + 2] && memcmp(&str[0], &str[len], 3))
        len++;
      if (memcmp(&str[0], &str[len], 3)) {
        result->error = UnterminatedStringConstant;
        result->failure_offset = position;
        return;
      }
      token = new InputToken(_StringConstant, string(str + 3, len - 3), 0, 0, position, len + 3);
    }

    // check for normal strings (single or double quoted)
    if (!token && (str[0] == '\"' || str[0] == '\'')) {
      int len = 1;
      while (str[len] && str[0] != str[len])
        len++;
      if (str[0] != str[len]) {
        result->error = UnterminatedStringConstant;
        result->failure_offset = position;
        return;
      }
      token = new InputToken(_StringConstant, string(str + 1, len - 1), 0, 0, position, len + 1);
    }

    // check for integers / floats
    if (!token) {
      const char* match = str;
      int match_length = 0;
      bool float_match = false;
      bool hex_match = false;

      // check for floats with leading .: .[0-9]+([eE][+-]?[0-9]+)?
      if (match[0] == '.' && is_digit(match[1])) {
        match++;
        while (is_digit(match[0]))
          match++;
        if (match[0] == 'e' || match[0] == 'E') {
          match++;
          if (match[0] == '+' || match[0] == '-')
            match++;
          if (!is_digit(match[0])) {
            result->error = BadScientificNotation;
            result->failure_offset = position;
            return;
          }
          while (is_digit(match[0]))
            match++;
        }
        match_length = match - str;
        float_match = true;
      }

      // check for floats: [0-9]+.[0-9]*([eE][+-]?[0-9]+)?
      // also checks for ints; if it doesn't have a . or an e[] then it's an int
      match = str;
      if (is_digit(match[0])) {
        match++;
        while (is_digit(match[0]))
          match++;
        if (match[0] == '.') {
          match++;
          while (is_digit(match[0]))
            match++;
          float_match = true;
        }
        if (match[0] == 'e' || match[0] == 'E') {
          match++;
          if (match[0] == '+' || match[0] == '-')
            match++;
          if (!is_digit(match[0])) {
            result->error = BadScientificNotation;
            result->failure_offset = position;
            return;
          }
          while (is_digit(match[0]))
            match++;
          float_match = true;
        }
        if (!float_match && match[0] == 'L')
          match++;
        match_length = match - str;
      }

      // check for hexadecimal: 0x[0-9A-Fa-f]+
      match = str;
      if (match[0] == '0' && (match[1] == 'x' || match[1] == 'X') && is_hex_digit(match[2])) {
        match += 3;
        while (is_hex_digit(match[0]))
          match++;
        match_length = match - str;
        hex_match = true;
      }

      // if there was a match, parse out the number
      if (match_length && float_match) {
        double data;
        sscanf(str, "%lg", &data);
        token = new InputToken(_Float, string(str, match_length), data, 0, position, match_length);
      } else if (match_length && !float_match) {
        long long data;
        sscanf(str, hex_match ? "%llX" : "%lld", &data);
        token = new InputToken(_Integer, string(str, match_length), 0, data, position, match_length);
      }
    }

    // check for symbolic static tokens
    if (!token) {
      TokenType type = _Dynamic;
      int len = 0;
      for (size_t x = 0; x < sizeof(symbolic_static_tokens) / sizeof(symbolic_static_tokens[0]); x++) {
        if (!memcmp(str, symbolic_static_tokens[x].text, strlen(symbolic_static_tokens[x].text))) {
          type = symbolic_static_tokens[x].type;
          len = strlen(symbolic_static_tokens[x].text);
          break;
        }
      }
      if (type != _Dynamic)
        token = new InputToken(type, "", 0, 0, position, len);
    }



    // if we haven't read a token, there's a syntax error
    // if we have read a token, advance past it
    if (!token) {
      result->error = BadToken;
      result->failure_offset = position;
      return;
    } else {
      position += token->text_length;
      str += token->text_length;
    }

    // keep track of open parens/braces/brackets
    int next_indent;
    switch (token->type) {
      case _OpenParen:
      case _OpenBracket:
      case _OpenBrace:
        paren_stack.push_back(token->type);
        result->tokens.push_back(*token);
        break;

      // fail if a close paren/brace/bracket doesn't match
      case _CloseParen:
      case _CloseBracket:
      case _CloseBrace:
        if (paren_stack.size() < 1 || paren_stack.back() != (token->type - 1)) {
          result->error = UnmatchedParenthesis;
          result->failure_offset = position;
          delete token;
          return;
        }
        paren_stack.pop_back();
        result->tokens.push_back(*token);
        break;

      // a newline might be followed by a nonzero number of indents/unindents,
      // but only if the paren_stack is empty
      case _Newline:
        if (paren_stack.size() > 0)
          break;
        result->tokens.push_back(*token);

        // skip any blank lines - we don't enforce indentation for them
        int blank_line_len;
        while ((blank_line_len = get_blank_line_length(str))) {
          position += blank_line_len;
          str += blank_line_len;
        }

        next_indent = get_line_indent(str);
        if (next_indent > indent_levels[indent]) {
          indent_levels.push_back(next_indent);
          indent++;
          result->tokens.push_back(InputToken(_Indent, "", 0, 0, position, 0));
        } else while (next_indent < indent_levels[indent]) {
          indent_levels.pop_back();
          indent--;
          result->tokens.push_back(InputToken(_Unindent, "", 0, 0, position, 0));
        }
        if (indent_levels[indent] != next_indent) {
          result->error = MisalignedUnindent;
          result->failure_offset = position;
          delete token;
          return;
        }
        position += next_indent;
        str += next_indent;
        break;

      // throw out backslash-newlines
      case _BackslashNewline:
        break;

      // everything else goes directly onto the result list
      default:
        result->tokens.push_back(*token);
    }

    delete token;
    token = NULL;
  }

  // postprocessing steps

  // delete comments
  for (size_t x = 0; x < result->tokens.size(); x++) {
    if (result->tokens[x].type == _Comment) {
      if (x == result->tokens.size() - 1)
        result->tokens.pop_back(); // the last token is a comment; delete it
      else {
        if (result->tokens[x + 1].type != _Newline) {
          result->error = IncompleteLexing;
          result->failure_offset = x;
          return;
        }
        result->tokens.erase(result->tokens.begin() + x, result->tokens.begin() + x + 1);
        x--;
      }
    }
  }

  // remove leading newlines
  while (result->tokens.size() > 0 && result->tokens[0].type == _Newline)
    result->tokens.erase(result->tokens.begin());

  // replace composite tokens, duplicate newlines, and semicolons
  for (size_t x = 0; x < result->tokens.size() - 1; x++) {
    if (result->tokens[x].type == Is && result->tokens[x + 1].type == Not) {
      result->tokens.erase(result->tokens.begin() + x, result->tokens.begin() + x + 1);
      result->tokens[x] = InputToken(IsNot, "", 0, 0, x, 0);
    } else if (result->tokens[x].type == Not && result->tokens[x + 1].type == In) {
      result->tokens.erase(result->tokens.begin() + x, result->tokens.begin() + x + 1);
      result->tokens[x] = InputToken(NotIn, "", 0, 0, x, 0);
    } else if (result->tokens[x].type == _Semicolon) {
      result->tokens[x].type = _Newline;
      x--;
    } else if (result->tokens[x].type == _Newline && result->tokens[x + 1].type == _Newline) {
      result->tokens.erase(result->tokens.begin() + x, result->tokens.begin() + x + 1);
      x--;
    }
  }

  // make sure it ends with a newline
  if (result->tokens.size() == 0 || result->tokens.back().type != _Newline) {
    result->tokens.push_back(InputToken(_Newline, "", 0, 0, result->tokens.size(), 0));
  }

  // close any indents that may be open
    while (indent_levels.back() > 0) {
    indent_levels.pop_back();
    result->tokens.push_back(InputToken(_Unindent, "", 0, 0, position, 0));
  }
}
