#ifndef _PARSE_HH
#define _PARSE_HH

#include "lex.hh"
#include "ast.hh"

enum ParseError {
  NoParseError = 0,
  UnimplementedFeature,
  InvalidIndentationChange,
  InvalidStartingTokenType,
  ExtraDataAfterLine,
  UnbalancedImportStatement,
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

struct PythonAST {
  shared_ptr<ModuleStatement> root;
  ParseError error;
  int failure_offset;
  string failure_explanation;

  PythonAST();
};

void parse_token_stream(const TokenStream*, PythonAST*);
const char* name_for_parse_error(ParseError);

#endif // _PARSE_HH