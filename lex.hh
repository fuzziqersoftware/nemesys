#ifndef _LEX_HH
#define _LEX_HH

#include <string>

#include "source_file.hh"

enum TokenType {
  _Dynamic = 0,
  _StringConstant,
  _Integer,
  _Float,
  _Indent,
  _Unindent,
  _Comment,
  Print,
  Del,
  Pass,
  Break,
  Continue,
  Return,
  Raise,
  Import,
  From,
  As,
  Def,
  Global,
  Exec,
  Assert,
  If,
  Else,
  Elif,
  With,
  While,
  For,
  In,
  NotIn,
  Not,
  And,
  Or,
  Try,
  Except,
  Finally,
  Lambda,
  _Colon,
  Class,
  Yield,
  _At,
  _OpenParen,
  _CloseParen, // must be _OpenParen + 1
  _Newline,
  _Equals,
  _Comma,
  _Asterisk,
  _DoubleAsterisk,
  _Semicolon,
  _PlusEquals,
  _MinusEquals,
  _AsteriskEquals,
  _SlashEquals,
  _PercentEquals,
  _AndEquals,
  _OrEquals,
  _XorEquals,
  _LeftShiftEquals,
  _RightShiftEquals,
  _DoubleTimesEquals,
  _DoubleSlashEquals,
  _LeftShift,
  _RightShift,
  _Dot,
  _Plus,
  _Minus,
  _Slash,
  _LessThan,
  _GreaterThan,
  _Equality,
  _GreaterOrEqual,
  _LessOrEqual,
  _NotEqual,
  Is,
  IsNot,
  _Or,
  _Xor,
  _And,
  _Percent,
  _DoubleSlash,
  _Tilde,
  _OpenBracket,
  _CloseBracket, // must be _OpenBracket + 1
  _OpenBrace,
  _CloseBrace, // must be _OpenBrace + 1
  _Backtick,
  _BackslashNewline, // these are eaten up by the lexer (not produced in output)
  _InvalidToken, // these are guaranteed to never be produced by the lexer
};

struct InputToken {
  // struct to represent a token in an input stream.

  InputToken(TokenType, const string&, float, long long, int, int);
  ~InputToken();

  TokenType type;
  string string_data;
  double float_data;
  long long int_data;

  int text_offset;
  int text_length;
};

enum TokenizationError {
  NoLexError = 0,
  UnmatchedParenthesis,
  UnmatchedBrace,
  UnmatchedBracket,
  MisalignedUnindent,
  BadToken,
  UnterminatedStringConstant,
  BadScientificNotation,
  IncompleteLexing,
};

struct TokenStream {
  vector<InputToken> tokens;
  TokenizationError error;
  int failure_offset;
};

bool is_open_bracket_token(TokenType);
bool is_close_bracket_token(TokenType);
bool token_requires_opener(TokenType);
TokenType get_closing_bracket_token_type(TokenType);
bool is_static_token(TokenType);
bool is_operator_token(TokenType);
const char* name_for_token_type(TokenType);
const char* name_for_tokenization_error(TokenizationError);
void tokenize_string(const char*, TokenStream*);

#endif // _LEX_HH