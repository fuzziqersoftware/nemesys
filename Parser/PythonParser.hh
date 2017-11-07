#pragma once

#include <stdint.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "PythonLexer.hh"
#include "PythonASTNodes.hh"


class PythonParser {
public:
  enum class ParseError {
    NoParseError = 0,
    UnimplementedFeature,
    InvalidIndentationChange,
    InvalidStartingTokenType,
    ExtraDataAfterLine,
    InvalidDynamicList,
    SyntaxError,
    UnexpectedEndOfStream,
    BracketingError,
    IncompleteParsing,

    // expression parsing errors
    IncompleteTernaryOperator,
    IncompleteLambdaDefinition,
    IncompleteGeneratorExpression,
    IncompleteExpressionParsing,
    IncompleteDictItem,
    TooManyArguments,
    InvalidAssignment,
  };

  static const char* name_for_parse_error(ParseError);

  class parse_error : public std::runtime_error {
  public:
    parse_error(ParseError error, size_t token_num, size_t file_offset,
        size_t line_num, const std::string& explanation);

    ParseError error;
    size_t token_num;
    size_t file_offset;
    size_t line_num;
    std::string explanation;
  };

  PythonParser(std::shared_ptr<const PythonLexer> lexer);
  ~PythonParser() = default;

  std::shared_ptr<const PythonLexer> get_lexer() const;

  // note: this returns a mutable object (unlike the lexer) because the AST can
  // have annotations made inside it
  std::shared_ptr<ModuleStatement> get_root();

private:
  std::shared_ptr<const PythonLexer> lexer;
  ssize_t token_num;

  std::shared_ptr<ModuleStatement> root;

  const PythonLexer::Token& head_token();
  const PythonLexer::Token& token_at(ssize_t offset);
  void advance_token(ssize_t count = 1);

  void expect_condition(bool condition, ParseError error,
      const std::string& explanation = "");
  void expect_token_type(PythonLexer::Token::TokenType type, ParseError error,
      const std::string& explanation = "");
  void expect_offset(ssize_t offset, ParseError error,
      const std::string& explanation = "");

  std::pair<ssize_t, ssize_t> find_bracketed_any(
      const std::vector<PythonLexer::Token::TokenType>& types,
      ssize_t end_offset, bool find_last = false);
  ssize_t find_bracketed_end(PythonLexer::Token::TokenType type,
      ssize_t end_offset, bool find_last = false);

  std::vector<std::string> parse_dynamic_list();

  std::shared_ptr<Expression> parse_binary_operator(ssize_t operator_offset,
      ssize_t end_offset, BinaryOperator oper);
  std::shared_ptr<Expression> parse_unary_operator(ssize_t end_offset,
      UnaryOperator oper);
  std::vector<std::shared_ptr<Expression>> parse_expression_list(
      ssize_t end_offset, bool lvalue_reference = false);
  std::shared_ptr<Expression> parse_expression_tuple(ssize_t end_offset,
      bool lvalue_reference = false);

  std::vector<std::pair<std::shared_ptr<Expression>, std::shared_ptr<Expression>>>
      parse_dict_item_list(ssize_t end_offset);

  std::shared_ptr<TypeAnnotation> parse_type_annotation();

  FunctionArguments parse_function_argument_definition(ssize_t end_offset,
      bool allow_type_annotations = true);
  std::shared_ptr<Expression> parse_expression(ssize_t end_offset,
      bool lvalue_reference = false);

  std::vector<std::shared_ptr<Statement>> parse_suite_from_colon(ssize_t end_offset);
  std::shared_ptr<SimpleStatement> parse_simple_statement(ssize_t end_offset);
  std::vector<std::shared_ptr<Statement>> parse_compound_statement_suite(
      ssize_t end_offset);

  std::shared_ptr<Expression> parse_expression();
};
