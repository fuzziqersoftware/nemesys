#pragma once

#include <memory>
#include <string>
#include <vector>

#include "SourceFile.hh"


std::string unescape_bytes(const char* s, size_t size);
std::string unescape_bytes(const std::string& s);
std::wstring unescape_unicode(const char* s, size_t size);
std::wstring unescape_unicode(const std::string& s);
std::string escape(const char* s, size_t size);
std::string escape(const std::string& s);
std::string escape(const wchar_t* s, size_t size);
std::string escape(const std::wstring& s);


class PythonLexer {
public:
  enum class TokenizationError {
    NoLexError = 0,
    UnmatchedParenthesis,
    UnmatchedBrace,
    UnmatchedBracket,
    MisalignedUnindent,
    BadToken,
    UnterminatedString,
    BadScientificNotation,
    IncompleteLexing,
  };

  static const char* name_for_tokenization_error(TokenizationError);

  struct Token {
    enum class TokenType {
      _Dynamic = 0,
      _BytesConstant,
      _UnicodeConstant,
      _Integer,
      _Float,
      _Indent,
      _Unindent,
      _Comment,
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
      Nonlocal,
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
      _Arrow, // used for type annotations
      _InvalidToken, // these are guaranteed to never be produced by the lexer
    };

    static bool is_open_bracket_token(TokenType);
    static bool is_close_bracket_token(TokenType);
    static bool token_requires_opener(TokenType);
    static TokenType get_closing_bracket_token_type(TokenType);
    static bool is_static_token(TokenType);
    static bool is_operator_token(TokenType);
    static const char* name_for_token_type(TokenType);

    TokenType type;
    std::string string_data;
    double float_data;
    int64_t int_data;

    size_t text_offset;
    size_t text_length;

    Token(TokenType type, const std::string& string_data, float float_data,
        int64_t int_data, size_t text_offset, size_t text_length);

    std::string str() const;
  };

  class tokenization_error : public std::runtime_error {
  public:
    tokenization_error(TokenizationError error, size_t offset, size_t line);

    TokenizationError error;
    size_t offset;
    size_t line;
  };

  PythonLexer(std::shared_ptr<const SourceFile> source);
  ~PythonLexer() = default;

  std::shared_ptr<const SourceFile> get_source() const;
  const std::vector<Token>& get_tokens() const;

private:
  std::shared_ptr<const SourceFile> source;
  std::vector<Token> tokens;
};
