#include "PythonParser.hh"

#include <phosg/Strings.hh>

#include "PythonLexer.hh"

using namespace std;

using TokenType = PythonLexer::Token::TokenType;


static const vector<const char*> error_names = {
  "NoParseError",
  "UnimplementedFeature",
  "InvalidIndentationChange",
  "InvalidStartingTokenType",
  "ExtraDataAfterLine",
  "InvalidDynamicList",
  "SyntaxError",
  "UnexpectedEndOfStream",
  "BracketingError",
  "IncompleteParsing",
  "IncompleteTernaryOperator",
  "IncompleteLambdaDefinition",
  "IncompleteGeneratorExpression",
  "IncompleteExpressionParsing",
  "IncompleteDictItem",
  "TooManyArguments",
  "InvalidAssignment",
};

const char* PythonParser::name_for_parse_error(ParseError type) {
  return error_names.at(static_cast<size_t>(type));
}



PythonParser::parse_error::parse_error(ParseError error, size_t token_num,
    size_t file_offset, size_t line_num, const std::string& explanation) :
    runtime_error(string_printf(
      "parsing failed: %s (%s) at token %zu (offset %zu, line %zu)",
      PythonParser::name_for_parse_error(error), explanation.c_str(), token_num,
      file_offset, line_num)),
    error(error), token_num(token_num), file_offset(file_offset),
    line_num(line_num), explanation(explanation) { }



PythonParser::PythonParser(shared_ptr<const PythonLexer> lexer) : lexer(lexer),
    token_num(0) {
  auto items = this->parse_compound_statement_suite(
      this->lexer->get_tokens().size());
  this->root.reset(new ModuleStatement(move(items), 0));
}

shared_ptr<ModuleStatement> PythonParser::get_root() {
  return this->root;
}



const PythonLexer::Token& PythonParser::head_token() {
  return this->lexer->get_tokens()[this->token_num];
}

const PythonLexer::Token& PythonParser::token_at(ssize_t offset) {
  return this->lexer->get_tokens()[offset];
}

void PythonParser::advance_token(ssize_t count) {
  this->token_num += count;
}



void PythonParser::expect_condition(bool condition, ParseError error,
    const string& explanation) {
  if (!condition) {
    size_t token_offset = this->head_token().text_offset;
    size_t line_num = this->lexer->get_source()->line_number_of_offset(token_offset);
    throw parse_error(error, this->token_num, token_offset, line_num, explanation);
  }
}

void PythonParser::expect_token_type(TokenType type, ParseError error,
    const string& explanation) {
  this->expect_condition(this->head_token().type == type, error, explanation);
}

void PythonParser::expect_offset(ssize_t offset, ParseError error,
    const string& explanation) {
  this->expect_condition(this->token_num == offset, error, explanation);
}

// returns a pair of (offset, type_index)
pair<ssize_t, ssize_t> PythonParser::find_bracketed_any(
    const vector<TokenType>& types, ssize_t end_offset, bool find_last) {
  // same as find_bracketed_end but can search for multiple token types.

  ssize_t found_offset = -1;
  ssize_t found_index = -1;
  vector<TokenType> open_stack;
  for (ssize_t offset = this->token_num; offset < end_offset; offset++) {
    // check if it matches any of the given tokens
    if (open_stack.empty()) {
      for (ssize_t x = 0; x < types.size(); x++) {
        if (this->token_at(offset).type != types[x]) {
          continue;
        }
        if (!find_last) {
          return make_pair(offset, x);
        }
        found_offset = offset;
        found_index = x;
      }
    }

    if ((open_stack.size() > 0) &&
        (open_stack.back() == this->token_at(offset).type)) {
      open_stack.pop_back(); // expected close brace/bracket/whatever
    } else if (PythonLexer::Token::is_open_bracket_token(
        this->token_at(offset).type)) {
      open_stack.emplace_back(
          PythonLexer::Token::get_closing_bracket_token_type(
            this->token_at(offset).type));
    } else {
      this->expect_condition(
          !PythonLexer::Token::token_requires_opener(
            this->token_at(offset).type), ParseError::BracketingError,
          "found a closing token with no matching open token");
    }
  }

  return make_pair(found_offset, found_index);
}

ssize_t PythonParser::find_bracketed_end(TokenType type, ssize_t end_offset,
    bool find_last) {
  vector<PythonLexer::Token::TokenType> types;
  types.emplace_back(type);
  return find_bracketed_any(types, end_offset, find_last).first;
}

vector<string> PythonParser::parse_dynamic_list() {
  // imperfect comma-separated _Dynamic list parsing. some "features":
  // - the list can end with a comma not followed by a _Dynamic
  // - the _Dynamics don't have to be comma-separated
  // I think these "features" are ok though because I'm lazy.
  vector<string> result;
  this->expect_token_type(TokenType::_Dynamic, ParseError::InvalidDynamicList);
  while (this->head_token().type == TokenType::_Dynamic) {
    result.emplace_back(this->head_token().string_data);
    this->advance_token();
    if (this->head_token().type == TokenType::_Comma) {
      this->advance_token();
    }
  }
  return result;
}



// expression parsing

shared_ptr<Expression> PythonParser::parse_binary_operator(
    ssize_t operator_offset, ssize_t end_offset, BinaryOperator oper) {

  size_t offset = this->head_token().text_offset;
  auto left = this->parse_expression(operator_offset);
  this->expect_offset(operator_offset, ParseError::IncompleteParsing,
      "left side of binary operator is incomplete");
  this->advance_token();
  auto right = this->parse_expression(end_offset);
  this->expect_offset(end_offset, ParseError::IncompleteParsing,
      "right side of binary operator is incomplete");
  return shared_ptr<Expression>(new BinaryOperation(oper, left, right, offset));
}

shared_ptr<Expression> PythonParser::parse_unary_operator(ssize_t end_offset,
    UnaryOperator oper) {

  size_t offset = this->head_token().text_offset;
  this->advance_token();
  auto expr = this->parse_expression(end_offset);
  this->expect_offset(end_offset, ParseError::IncompleteParsing,
      "argument of unary operator is incomplete");
  return shared_ptr<Expression>(new UnaryOperation(oper, expr, offset));
}

vector<shared_ptr<Expression>> PythonParser::parse_expression_list(
    ssize_t end_offset, bool lvalue_reference) {
  vector<shared_ptr<Expression>> items;
  while (this->token_num < end_offset) {
    ssize_t comma_offset = this->find_bracketed_end(TokenType::_Comma, end_offset);
    if (comma_offset < 0) {
      comma_offset = end_offset;
    }
    items.emplace_back(this->parse_expression(comma_offset, lvalue_reference));
    expect_offset(comma_offset, ParseError::IncompleteParsing,
        "expression in list is incomplete");
    if (comma_offset < end_offset) {
      this->advance_token();
    }
  }
  return items;
}

shared_ptr<Expression> PythonParser::parse_expression_tuple(
    ssize_t end_offset, bool lvalue_reference) {

  // if the expression contains a comma at all, then it's a tuple
  ssize_t comma_offset = this->find_bracketed_end(TokenType::_Comma, end_offset);
  if (comma_offset >= 0) {
    size_t offset = this->head_token().text_offset;
    auto items = this->parse_expression_list(end_offset, lvalue_reference);
    if (lvalue_reference) {
      return shared_ptr<Expression>(new TupleLValueReference(move(items), offset));
    } else {
      return shared_ptr<Expression>(new TupleConstructor(move(items), offset));
    }

  // there's no comma; it's a standard Expression. don't wrap it in anything
  } else {
    return this->parse_expression(end_offset, lvalue_reference);
  }
}

vector<pair<shared_ptr<Expression>, shared_ptr<Expression>>>
PythonParser::parse_dict_item_list(ssize_t end_offset) {
  vector<pair<shared_ptr<Expression>, shared_ptr<Expression>>> ret;
  while (this->token_num < end_offset) {
    ssize_t comma_offset = this->find_bracketed_end(TokenType::_Comma, end_offset);
    if (comma_offset < 0) {
      comma_offset = end_offset;
    }
    ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, comma_offset);
    this->expect_condition((colon_offset > 0) && (colon_offset < comma_offset),
        ParseError::IncompleteDictItem, "dict item does not contain a colon");

    shared_ptr<Expression> key = this->parse_expression(colon_offset);
    this->expect_offset(colon_offset, ParseError::IncompleteParsing,
        "key in dict definition is incomplete");
    this->advance_token();
    shared_ptr<Expression> value = this->parse_expression(comma_offset);
    this->expect_offset(comma_offset, ParseError::IncompleteParsing,
        "value in dict definition is incomplete");
    if (comma_offset < end_offset) {
      this->advance_token();
    }

    ret.emplace_back(make_pair(key, value));
  }
  return ret;
}

vector<shared_ptr<ArgumentDefinition>>
PythonParser::parse_function_argument_definition(ssize_t end_offset) {
  vector<shared_ptr<ArgumentDefinition>> ret;
  while (this->token_num < end_offset) {
    ssize_t comma_offset = this->find_bracketed_end(TokenType::_Comma, end_offset);
    if (comma_offset == -1) {
      comma_offset = end_offset;
    }
    size_t offset = this->head_token().text_offset;

    // if there's a * or **, it's a *args or **kwargs. expect a _Dynamic
    // followed by maybe a _Comma
    ArgumentMode mode = ArgumentMode::DefaultArgMode;
    if (this->head_token().type == TokenType::_Asterisk) {
      mode = ArgumentMode::ArgListMode;
    }
    if (this->head_token().type == TokenType::_DoubleAsterisk) {
      mode = ArgumentMode::KeywordArgListMode;
    }

    if (mode != ArgumentMode::DefaultArgMode) {
      this->advance_token();
      this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
          "expected name for args/kwargs variable");
      ret.emplace_back(new ArgumentDefinition(this->head_token().string_data,
          NULL, mode, offset));
      this->advance_token();

    // else it's a normal arg
    } else {
      const string& name = this->head_token().string_data;
      this->advance_token();

      // if there's a top-level =, then it's a kwarg
      shared_ptr<Expression> default_value;
      if (this->head_token().type == TokenType::_Equals) {
        this->advance_token();
        default_value = parse_expression(comma_offset);
      }
      ret.emplace_back(new ArgumentDefinition(name, default_value, mode, offset));
    }

    if (comma_offset < end_offset) {
      this->expect_token_type(TokenType::_Comma, ParseError::IncompleteParsing,
          "function argument is incomplete");
      this->advance_token(); // skip comma
    }
  }
  return ret;
}

vector<shared_ptr<ArgumentDefinition>>
PythonParser::parse_function_call_arguments(ssize_t end_offset) {
  // TODO: reduce code duplication with this function and the ebove
  vector<shared_ptr<ArgumentDefinition>> ret;
  while (this->token_num < end_offset) {
    ssize_t comma_offset = this->find_bracketed_end(TokenType::_Comma, end_offset);
    if (comma_offset == -1) {
      comma_offset = end_offset;
    }
    size_t offset = this->head_token().text_offset;

    // if there's a * or **, it's a *args or **kwargs; change the arg mode
    ArgumentMode mode = ArgumentMode::DefaultArgMode;
    if (this->head_token().type == TokenType::_Asterisk) {
      mode = ArgumentMode::ArgListMode;
      this->advance_token();
    }
    if (this->head_token().type == TokenType::_DoubleAsterisk) {
      mode = ArgumentMode::KeywordArgListMode;
      this->advance_token();
    }

    // if there's a top-level =, then it's a kwarg
    string name;
    ssize_t equals_offset = find_bracketed_end(TokenType::_Equals, comma_offset);
    if (equals_offset >= 0) {
      this->expect_condition((mode == ArgumentMode::DefaultArgMode) &&
          (equals_offset == this->token_num + 1), ParseError::SyntaxError,
          "found =, but not immediately following name");
      this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
          "expected name for keyword argument");
      name = this->head_token().string_data;
      this->advance_token();
      this->expect_token_type(TokenType::_Equals, ParseError::SyntaxError,
          "expected = immediately following keyword argument name");
      this->advance_token();
    }

    shared_ptr<Expression> default_value = this->parse_expression(comma_offset);
    ret.emplace_back(new ArgumentDefinition(name, default_value, mode, offset));

    if (comma_offset < end_offset) {
      this->expect_token_type(TokenType::_Comma, ParseError::IncompleteParsing,
          "function call argument is incomplete");
      this->advance_token(); // skip comma
    }
  }
  return ret;
}

shared_ptr<Expression> PythonParser::parse_expression(ssize_t end_offset,
    bool lvalue_reference) {
  size_t offset = this->head_token().text_offset;

  if (!lvalue_reference) {

    // 16. lambda
    if (this->head_token().type == TokenType::Lambda) {
      this->advance_token();

      ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, end_offset);
      this->expect_condition((colon_offset >= 0) && (colon_offset < end_offset),
          ParseError::IncompleteLambdaDefinition, "lambda has no colon");

      auto args = this->parse_function_argument_definition(colon_offset);
      this->expect_offset(colon_offset, ParseError::IncompleteParsing,
          "lambda argspec is incomplete");
      this->advance_token();
      auto result = this->parse_expression(end_offset);
      this->expect_offset(end_offset, ParseError::IncompleteParsing,
          "lambda body is incomplete");
      return shared_ptr<LambdaDefinition>(new LambdaDefinition(args, result, offset));
    }

    // 15. x if y else z
    {
      ssize_t if_offset = this->find_bracketed_end(TokenType::If, end_offset, true);
      if ((if_offset > this->token_num) && (if_offset < end_offset)) {
        ssize_t else_offset = this->find_bracketed_end(TokenType::Else, end_offset, true);
        this->expect_condition((else_offset > if_offset) &&
            (else_offset < end_offset), ParseError::IncompleteTernaryOperator);

        auto left = this->parse_expression(if_offset);
        this->expect_offset(if_offset, ParseError::IncompleteParsing,
            "left side of ternary operation is incomplete");
        this->advance_token();
        auto center = this->parse_expression(else_offset);
        this->expect_offset(else_offset, ParseError::IncompleteParsing,
            "center side of ternary operation is incomplete");
        this->advance_token();
        auto right = this->parse_expression(end_offset);
        this->expect_offset(end_offset, ParseError::IncompleteParsing,
            "right side of ternary operation is incomplete");
        return shared_ptr<TernaryOperation>(new TernaryOperation(
            TernaryOperator::IfElse, left, center, right, offset));
      }
    }

    // 14. or
    {
      ssize_t oper_offset = this->find_bracketed_end(TokenType::Or, end_offset, true);
      if ((oper_offset > this->token_num) && (oper_offset < end_offset)) {
        return this->parse_binary_operator(oper_offset, end_offset,
            BinaryOperator::LogicalOr);
      }
    }

    // 13. and
    {
      ssize_t oper_offset = this->find_bracketed_end(TokenType::And, end_offset, true);
      if ((oper_offset > this->token_num) && (oper_offset < end_offset)) {
        return this->parse_binary_operator(oper_offset, end_offset,
            BinaryOperator::LogicalAnd);
      }
    }

    // 12. not
    if (this->head_token().type == TokenType::Not) {
      return this->parse_unary_operator(end_offset, UnaryOperator::LogicalNot);
    }

    // 11. in, not in, is, is not, <, <=, >, >=, <>, !=, ==
    {
      static const vector<BinaryOperator> operators_level11 = {
          BinaryOperator::In, BinaryOperator::NotIn, BinaryOperator::Is,
          BinaryOperator::IsNot, BinaryOperator::LessThan,
          BinaryOperator::LessOrEqual, BinaryOperator::GreaterThan,
          BinaryOperator::GreaterOrEqual, BinaryOperator::NotEqual,
          BinaryOperator::Equality};
      static const vector<TokenType> operator_tokens_level11 = {
          TokenType::In, TokenType::NotIn, TokenType::Is, TokenType::IsNot,
          TokenType::_LessThan, TokenType::_LessOrEqual, TokenType::_GreaterThan,
          TokenType::_GreaterOrEqual, TokenType::_NotEqual, TokenType::_Equality};
      auto find_ret = this->find_bracketed_any(operator_tokens_level11,
          end_offset, true);
      if ((find_ret.first > this->token_num) && (find_ret.first < end_offset)) {
        return parse_binary_operator(find_ret.first, end_offset,
            operators_level11[find_ret.second]);
      }
    }

    // 10. |
    {
      ssize_t oper_offset = this->find_bracketed_end(TokenType::_Or, end_offset, true);
      if ((oper_offset > this->token_num) && (oper_offset < end_offset)) {
        return this->parse_binary_operator(oper_offset, end_offset, BinaryOperator::Or);
      }
    }

    // 9. ^
    {
      ssize_t oper_offset = this->find_bracketed_end(TokenType::_Xor, end_offset, true);
      if ((oper_offset > this->token_num) && (oper_offset < end_offset)) {
        return this->parse_binary_operator(oper_offset, end_offset, BinaryOperator::Xor);
      }
    }

    // 8. &
    {
      ssize_t oper_offset = this->find_bracketed_end(TokenType::_And, end_offset, true);
      if ((oper_offset > this->token_num) && (oper_offset < end_offset)) {
        return this->parse_binary_operator(oper_offset, end_offset, BinaryOperator::And);
      }
    }

    // 7. <<, >>
    {
      static const vector<BinaryOperator> operators_level7 = {
          BinaryOperator::LeftShift, BinaryOperator::RightShift};
      static const vector<TokenType> operator_tokens_level7 = {
          TokenType::_LeftShift, TokenType::_RightShift};
      auto find_ret = this->find_bracketed_any(operator_tokens_level7, end_offset,
          true);
      if ((find_ret.first > this->token_num) && (find_ret.first < end_offset)) {
        return this->parse_binary_operator(find_ret.first, end_offset,
            operators_level7[find_ret.second]);
      }
    }

    // 6. +, -
    // we assume these operators are binary unless there's (a) another operator or
    // (b) nothing on the left
    {
      static const vector<BinaryOperator> operators_level6 = {
          BinaryOperator::Addition, BinaryOperator::Subtraction};
      static const vector<TokenType> operator_tokens_level6 = {
          TokenType::_Plus, TokenType::_Minus};
      auto find_ret = this->find_bracketed_any(operator_tokens_level6, end_offset,
          true);
      if ((find_ret.first > this->token_num) && (find_ret.first < end_offset) &&
          !PythonLexer::Token::is_operator_token(
            this->token_at(find_ret.first - 1).type)) {
        return this->parse_binary_operator(find_ret.first, end_offset,
            operators_level6[find_ret.second]);
      }
    }

    // 5. *, /, //, %
    {
      static const vector<BinaryOperator> operators_level5 = {
          BinaryOperator::Multiplication,
          BinaryOperator::Division,
          BinaryOperator::IntegerDivision,
          BinaryOperator::Modulus};
      static const vector<TokenType> operator_tokens_level5 = {
          TokenType::_Asterisk, TokenType::_Slash, TokenType::_DoubleSlash,
          TokenType::_Percent};
      auto find_ret = this->find_bracketed_any(operator_tokens_level5, end_offset,
          true);
      if ((find_ret.first > this->token_num) && (find_ret.first < end_offset)) {
        return parse_binary_operator(find_ret.first, end_offset,
            operators_level5[find_ret.second]);
      }
    }

    // 4. +x, -x, ~x
    if (this->head_token().type == TokenType::_Plus) {
      return this->parse_unary_operator(end_offset, UnaryOperator::Positive);
    }
    if (this->head_token().type == TokenType::_Minus) {
      return this->parse_unary_operator(end_offset, UnaryOperator::Negative);
    }
    if (this->head_token().type == TokenType::_Tilde) {
      return this->parse_unary_operator(end_offset, UnaryOperator::Not);
    }

    // 3. ** (note: The power operator ** binds less tightly than an arithmetic or
    // bitwise unary operator on its right, that is, 2**-1 is 0.5.)
    {
      ssize_t oper_offset = this->find_bracketed_end(TokenType::_DoubleAsterisk, end_offset,
          true);
      if ((oper_offset > this->token_num) && (oper_offset < end_offset)) {
        return parse_binary_operator(oper_offset, end_offset,
            BinaryOperator::Exponentiation);
      }
    }
  }

  // 2. x[y], x[y:z], x[y:z:w], x(y, z, ...), x.y
  // this is the only section that's valid in lvalue references.
  ssize_t bracket_offset = this->find_bracketed_end(TokenType::_OpenBracket, end_offset,
      true);
  ssize_t paren_offset = this->find_bracketed_end(TokenType::_OpenParen, end_offset, true);
  ssize_t dot_offset = this->find_bracketed_end(TokenType::_Dot, end_offset, true);
  ssize_t effective_offset = max(bracket_offset, max(dot_offset, paren_offset));

  if (effective_offset > this->token_num) {

    // array index / slice
    if (effective_offset == bracket_offset) {

      // parse array expr first
      shared_ptr<Expression> array = this->parse_expression(bracket_offset);
      this->expect_offset(bracket_offset, ParseError::IncompleteParsing,
          "array reference is incomplete");
      this->advance_token();

      // find the end of the index
      ssize_t bracket_end_offset = this->find_bracketed_end(TokenType::_CloseBracket,
          end_offset);
      this->expect_condition((bracket_end_offset > bracket_offset) &&
          (bracket_end_offset < end_offset), ParseError::BracketingError);

      // if there's a colon, it's a slice
      ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon,
          bracket_end_offset);
      if ((colon_offset > bracket_offset) &&
          (colon_offset < bracket_end_offset)) {

        shared_ptr<Expression> start_index;
        if (this->head_token().type != TokenType::_Colon) {
          start_index = this->parse_expression(colon_offset);
        }
        this->expect_token_type(TokenType::_Colon, ParseError::IncompleteParsing,
            "left side of slice is incomplete");
        this->advance_token();

        // find the second colon, if any
        colon_offset = this->find_bracketed_end(TokenType::_Colon, bracket_end_offset);

        // parse the end index of the slice, if given
        shared_ptr<Expression> end_index;
        auto head_type = this->head_token().type;
        if ((head_type != TokenType::_Colon) && (head_type != TokenType::_CloseBracket)) {
          end_index = this->parse_expression(
              colon_offset < 0 ? bracket_end_offset : colon_offset);
        }

        // if we're at a colon now, then there's a step expression also
        shared_ptr<Expression> step_size;
        if (this->head_token().type == TokenType::_Colon) {
          this->advance_token();
          step_size = this->parse_expression(bracket_end_offset);
        }

        // and now we should be at the close bracket
        this->expect_offset(bracket_end_offset, ParseError::IncompleteParsing,
            "right side of slice is incomplete");
        this->advance_token();

        if (lvalue_reference) {
          return shared_ptr<Expression>(new ArraySliceLValueReference(array,
              start_index, end_index, step_size, offset));
        } else {
          return shared_ptr<Expression>(new ArraySlice(array, start_index,
              end_index, step_size, offset));
        }

      // else it's just a normal array index
      } else {
        shared_ptr<Expression> index = this->parse_expression(bracket_end_offset);
        this->expect_offset(bracket_end_offset, ParseError::IncompleteParsing,
            "array index is incomplete");
        this->advance_token();

        if (lvalue_reference) {
          return shared_ptr<Expression>(new ArrayIndexLValueReference(array,
              index, offset));
        } else {
          return shared_ptr<Expression>(new ArrayIndex(array, index, offset));
        }
      }

    // argument list (not valid as lvalue reference)
    } else if (!lvalue_reference && (effective_offset == paren_offset)) {
      auto function = this->parse_expression(paren_offset);
      this->expect_offset(paren_offset, ParseError::IncompleteParsing,
          "function reference is incomplete");
      this->advance_token();

      ssize_t paren_end_offset = this->find_bracketed_end(TokenType::_CloseParen,
          end_offset);
      this->expect_condition((paren_end_offset > paren_offset) &&
          (paren_end_offset < end_offset), ParseError::BracketingError);

      auto args = this->parse_function_call_arguments(paren_end_offset);
      this->expect_offset(paren_end_offset, ParseError::IncompleteParsing,
          "function argument list is incomplete");
      this->advance_token();
      return shared_ptr<Expression>(new FunctionCall(function, args, offset));

    // attribute lookup
    } else if (effective_offset == dot_offset) {
      shared_ptr<Expression> base = this->parse_expression(dot_offset);
      this->expect_offset(dot_offset, ParseError::IncompleteParsing,
          "left side of attribute lookup is incomplete");
      this->advance_token();

      string name = this->head_token().string_data;
      this->advance_token();
      this->expect_offset(end_offset, ParseError::IncompleteParsing,
          "right side of attribute lookup is incomplete");

      if (lvalue_reference) {
        return shared_ptr<Expression>(new AttributeLValueReference(base, name, offset));
      } else {
        return shared_ptr<Expression>(new AttributeLookup(base, name, offset));
      }
    }
  }

  if (!lvalue_reference) {
    // 1. (expressions...), [expressions...], {key: value...}
    ssize_t brace_offset = this->find_bracketed_end(TokenType::_OpenBrace, end_offset, true);

    // list constructor/comprehension
    if (bracket_offset == this->token_num) {
      this->expect_condition(this->token_at(end_offset - 1).type == TokenType::_CloseBracket,
          ParseError::IncompleteParsing, "bracketed section is incomplete");
      this->advance_token();

      // if it's [], then it's an empty list
      if (this->token_num == (end_offset - 1)) {
        this->advance_token();
        return shared_ptr<ListConstructor>(new ListConstructor(offset));
      }

      // if there's a top-level 'for' and 'in', assume it's a comprehension
      ssize_t for_offset = this->find_bracketed_end(TokenType::For, end_offset - 1);
      if ((for_offset >= 0) && (for_offset < end_offset)) {
        auto item_pattern = this->parse_expression(for_offset);
        this->expect_offset(for_offset, ParseError::IncompleteParsing,
            "list comprehension expression is incomplete");
        this->advance_token();

        ssize_t in_offset = this->find_bracketed_end(TokenType::In, end_offset - 1);
        this->expect_condition((in_offset > for_offset) &&
            (in_offset < end_offset), ParseError::IncompleteGeneratorExpression);
        auto variable = this->parse_expression_tuple(in_offset, true);
        this->expect_offset(in_offset, ParseError::IncompleteParsing,
            "list comprehension unpacking format is incomplete");
        this->advance_token();

        ssize_t expr_end_offset = end_offset - 1;
        ssize_t if_offset = this->find_bracketed_end(TokenType::If, end_offset - 1);
        if ((if_offset > in_offset) && (in_offset < end_offset)) {
          expr_end_offset = if_offset;
        }

        auto source_data = this->parse_expression(expr_end_offset);
        this->expect_offset(expr_end_offset, ParseError::IncompleteParsing,
            "list comprehension source is incomplete");
        this->advance_token();

        shared_ptr<Expression> predicate;
        if ((if_offset > in_offset) && (in_offset < end_offset)) {
          predicate = this->parse_expression(end_offset - 1);
          this->expect_offset(end_offset - 1, ParseError::IncompleteParsing,
              "list comprehension condition is incomplete");
          this->advance_token();
        }
        return shared_ptr<Expression>(new ListComprehension(item_pattern,
            variable, source_data, predicate, offset));
      }

      // parse the list values
      auto items = this->parse_expression_list(end_offset - 1);
      this->expect_offset(end_offset - 1, ParseError::IncompleteParsing,
          "list constructor is incomplete");
      this->advance_token();
      return shared_ptr<Expression>(new ListConstructor(move(items), offset));

    // dict/set constructor/comprehension
    } else if (brace_offset == this->token_num) {
      this->expect_condition(this->token_at(end_offset - 1).type == TokenType::_CloseBrace,
          ParseError::IncompleteParsing, "braced section is incomplete");
      this->advance_token();

      // if it's {}, then it's an empty dict
      if (this->token_num == (end_offset - 1)) {
        this->advance_token();
        return shared_ptr<DictConstructor>(new DictConstructor(offset));
      }

      // if there's a top-level : then it's a dict; otherwise it's a set
      ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, end_offset - 1);
      bool is_dict = (colon_offset >= 0) && (colon_offset < end_offset);

      // if there's a top-level 'for' and 'in', assume it's a comprehension
      ssize_t for_offset = this->find_bracketed_end(TokenType::For, end_offset - 1, true);
      if ((for_offset >= 0) && (for_offset < end_offset)) {
        ssize_t in_offset = this->find_bracketed_end(TokenType::In, end_offset - 1, true);
        this->expect_condition((in_offset > for_offset) &&
            (in_offset < end_offset), ParseError::IncompleteGeneratorExpression);

        shared_ptr<Expression> key_pattern, item_pattern, source_data,
            predicate, variable;
        if (is_dict) {
          key_pattern = this->parse_expression(colon_offset);
          this->expect_offset(colon_offset, ParseError::IncompleteParsing,
              "dict comprehension key is incomplete");
          this->advance_token();
        }
        item_pattern = this->parse_expression(for_offset);
        this->expect_offset(for_offset, ParseError::IncompleteParsing,
            "dict/set comprehension value is incomplete");
        this->advance_token();

        variable = this->parse_expression_tuple(in_offset, true);
        this->expect_offset(in_offset, ParseError::IncompleteParsing,
            "dict/set comprehension unpacking format is incomplete");
        this->advance_token();

        ssize_t expr_end_offset = end_offset - 1;
        ssize_t if_offset = this->find_bracketed_end(TokenType::If, end_offset - 1);
        if ((if_offset > in_offset) && (in_offset < end_offset)) {
          expr_end_offset = if_offset;
        }

        source_data = this->parse_expression(expr_end_offset);
        this->expect_offset(expr_end_offset, ParseError::IncompleteParsing,
            "dict/set comprehension source is incomplete");
        this->advance_token();

        if ((if_offset > in_offset) && (in_offset < end_offset)) {
          predicate = this->parse_expression(end_offset - 1);
          this->expect_offset(end_offset - 1, ParseError::IncompleteParsing,
              "dict/set comprehension condition is incomplete");
          this->advance_token();
        }

        if (is_dict) {
          return shared_ptr<Expression>(new DictComprehension(key_pattern,
              item_pattern, variable, source_data, predicate, offset));
        }
        return shared_ptr<Expression>(new SetComprehension(item_pattern,
            variable, source_data, predicate, offset));
      }

      // else, it's just a simple constructor - parse the values
      if (is_dict) {
        auto items = this->parse_dict_item_list(end_offset - 1);
        this->expect_offset(end_offset - 1, ParseError::IncompleteParsing,
            "dict constructor is incomplete");
        this->advance_token();
        return shared_ptr<Expression>(new DictConstructor(move(items), offset));

      } else {
        auto items = this->parse_expression_list(end_offset - 1);
        this->expect_offset(end_offset - 1, ParseError::IncompleteParsing,
            "set constructor is incomplete");
        this->advance_token();
        return shared_ptr<Expression>(new SetConstructor(move(items), offset));
      }

    // tuple constructor
    } else if (paren_offset == this->token_num) {
      this->expect_condition(this->token_at(end_offset - 1).type == TokenType::_CloseParen,
          ParseError::IncompleteParsing, "parenthesized section is incomplete");
      this->advance_token();

      // parse the tuple values
      auto items = this->parse_expression_list(end_offset - 1);
      this->expect_offset(end_offset - 1, ParseError::IncompleteParsing,
          "tuple constructor is incomplete");
      this->advance_token();
      return shared_ptr<Expression>(new TupleConstructor(move(items), offset));
    }
  }

  // it's probably a constant if we get here
  if (this->token_num == end_offset - 1) {
    // if this is supposed to be an lvalue, the only valid token here is a
    // local variable reference (_Dynamic)
    if (lvalue_reference) {
      this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
          "cannot parse constant as lvalue");
      const auto& tok = this->head_token();
      this->advance_token();

      if ((tok.string_data == "True") || (tok.string_data == "False") || (tok.string_data == "None")) {
        expect_condition(false, ParseError::SyntaxError,
            "built-in constants cannot be reassigned");
      }

      // ref->base == NULL means a local variable reference
      return shared_ptr<Expression>(new AttributeLValueReference(NULL,
          tok.string_data, offset));

    } else {
      const auto& tok = this->head_token();
      this->advance_token();

      if (tok.type == TokenType::_Integer) {
        return shared_ptr<IntegerConstant>(new IntegerConstant(tok.int_data, offset));
      } else if (tok.type == TokenType::_Float) {
        return shared_ptr<FloatConstant>(new FloatConstant(tok.float_data, offset));
      } else if (tok.type == TokenType::_BytesConstant) {
        return shared_ptr<BytesConstant>(new BytesConstant(unescape_bytes(tok.string_data), offset));
      } else if (tok.type == TokenType::_UnicodeConstant) {
        return shared_ptr<UnicodeConstant>(new UnicodeConstant(unescape_unicode(tok.string_data), offset));
      }
      if (tok.type == TokenType::_Dynamic) {
        if (tok.string_data == "True") {
          return shared_ptr<TrueConstant>(new TrueConstant(offset));
        }
        if (tok.string_data == "False") {
          return shared_ptr<FalseConstant>(new FalseConstant(offset));
        }
        if (tok.string_data == "None") {
          return shared_ptr<NoneConstant>(new NoneConstant(offset));
        }
        return shared_ptr<VariableLookup>(new VariableLookup(tok.string_data, offset));
      }
    }
  }

  // uh-oh, we got something unparseable
  if (lvalue_reference) {
    this->expect_condition(false, ParseError::IncompleteExpressionParsing,
        "no lvalue parsing rules matched");
  } else {
    this->expect_condition(false, ParseError::IncompleteExpressionParsing,
        "no expression parsing rules matched");
  }
  return NULL;
}



// statement parsing

vector<shared_ptr<Statement>> PythonParser::parse_suite_from_colon(
    ssize_t end_offset) {

  // colon, newline, indent
  this->expect_token_type(TokenType::_Colon, ParseError::SyntaxError,
      "expected : before suite");
  this->advance_token();

  if (this->head_token().type == TokenType::_Newline) {
    this->advance_token();
    this->expect_token_type(TokenType::_Indent, ParseError::SyntaxError,
        "expected indentation after :");
    this->advance_token();

    // parse the suite
    ssize_t suite_end_offset = find_bracketed_end(TokenType::_Unindent, end_offset);
    this->expect_condition(suite_end_offset >= 0, ParseError::BracketingError);
    auto ret = this->parse_compound_statement_suite(suite_end_offset);
    this->expect_offset(suite_end_offset, ParseError::IncompleteParsing,
        "compound statement is incomplete");

    // better end with an _Unindent
    this->expect_token_type(TokenType::_Unindent, ParseError::SyntaxError,
        "expected unindentation after suite");
    this->advance_token();
    return ret;

  } else {
    // parse the suite
    ssize_t suite_end_offset = find_bracketed_end(TokenType::_Newline, end_offset);
    this->expect_condition(suite_end_offset >= 0, ParseError::BracketingError);
    auto ret = this->parse_compound_statement_suite(suite_end_offset + 1);
    this->expect_offset(suite_end_offset + 1, ParseError::IncompleteParsing,
        "inline compound statement is incomplete");
    return ret;
  }
}

shared_ptr<SimpleStatement> PythonParser::parse_simple_statement(
    ssize_t end_offset) {
  size_t offset = this->head_token().text_offset;

  static const vector<TokenType> augment_operator_tokens = {
    TokenType::_PlusEquals, TokenType::_MinusEquals, TokenType::_AsteriskEquals, TokenType::_SlashEquals, TokenType::_PercentEquals,
    TokenType::_AndEquals, TokenType::_OrEquals, TokenType::_XorEquals, TokenType::_LeftShiftEquals, TokenType::_RightShiftEquals,
    TokenType::_DoubleTimesEquals, TokenType::_DoubleSlashEquals};

  ssize_t operator_type;
  ssize_t operator_offset = this->find_bracketed_end(TokenType::_Equals, end_offset);
  for (operator_type = 0;
       (operator_type < static_cast<size_t>(AugmentOperator::_AugmentOperatorCount)) &&
         (operator_offset == -1);
       operator_type++) {
    operator_offset = this->find_bracketed_end(
        augment_operator_tokens[operator_type], end_offset);
  }
  operator_type--;

  // now operator_offset is the offset of the operator
  // and operator_type is the AugmentOperator enum value, or -1 for an equals

  // if there's no operator, then it's just a general expression - maybe a
  // function call
  if (operator_offset < 0) {
    return shared_ptr<SimpleStatement>(new ExpressionStatement(
        this->parse_expression_tuple(end_offset), offset));
  }

  if (operator_type == -1) { // simple assignment
    auto target = this->parse_expression_tuple(operator_offset, true);
    this->expect_condition(target->valid_lvalue(), ParseError::InvalidAssignment);
    this->expect_token_type(TokenType::_Equals, ParseError::IncompleteParsing,
        "left side of assignment is incomplete");
    this->advance_token();

    auto value = this->parse_expression_tuple(end_offset);
    return shared_ptr<SimpleStatement>(new AssignmentStatement(target, value, offset));

  } else {
    auto target = this->parse_expression_tuple(operator_offset, true);
    this->expect_condition(target->valid_lvalue(), ParseError::InvalidAssignment);
    this->expect_token_type(augment_operator_tokens[operator_type],
        ParseError::IncompleteParsing, "left side of augment is incomplete");
    this->advance_token();

    auto value = this->parse_expression_tuple(end_offset);
    return shared_ptr<SimpleStatement>(new AugmentStatement(
        static_cast<AugmentOperator>(operator_type), target, value, offset));
  }

  this->expect_condition(false, ParseError::IncompleteParsing,
      "no simple-statement parsing rules matched");
}

vector<shared_ptr<Statement>> PythonParser::parse_compound_statement_suite(
    ssize_t end_offset) {
  size_t offset = this->head_token().text_offset;
  vector<shared_ptr<Statement>> ret;

  // parser state local to the current indentation level
  vector<shared_ptr<Expression>> decorator_stack;
  shared_ptr<IfStatement> prev_if;
  shared_ptr<ForStatement> prev_for;
  shared_ptr<WhileStatement> prev_while;
  shared_ptr<TryStatement> prev_try;

  auto expect = [&](bool has_decorators, bool has_if, bool has_for, bool has_while, bool has_try) {
    this->expect_condition(has_decorators == !!decorator_stack.size(), ParseError::SyntaxError,
        "decorator stack state was lost");
    this->expect_condition(!has_if == !prev_if, ParseError::SyntaxError,
        "previous if block state was lost");
    this->expect_condition(!has_for == !prev_for, ParseError::SyntaxError,
        "previous for block state was lost");
    this->expect_condition(!has_while == !prev_while, ParseError::SyntaxError,
        "previous while block state was lost");
    this->expect_condition(!has_try == !prev_try, ParseError::SyntaxError,
        "previous try block state was lost");
  };

  auto expect_else = [&]() {
    ssize_t num_valid = 0;
    num_valid += !!prev_if;
    num_valid += !!prev_for;
    num_valid += !!prev_while;
    num_valid += !!prev_try;
    this->expect_condition((num_valid == 1) && (decorator_stack.size() == 0), ParseError::SyntaxError,
        "else block follows multiple kinds of other blocks");
  };

  auto clear = [&]() {
    decorator_stack.clear();
    prev_if = shared_ptr<IfStatement>();
    prev_for = shared_ptr<ForStatement>();
    prev_while = shared_ptr<WhileStatement>();
    prev_try = shared_ptr<TryStatement>();
  };

  while (this->token_num < end_offset) {

    bool newline_expected = true;
    bool should_clear_local = true;
    ssize_t line_end_offset = this->find_bracketed_end(TokenType::_Newline, end_offset);
    if (line_end_offset < 0) {
      line_end_offset = end_offset;
    }

    switch (this->head_token().type) {

      case TokenType::_Comment: // skip it. the next token will be _Newline; skip that too
        this->advance_token();
      case TokenType::_Newline: // skip this too
        should_clear_local = false;
        break;

      case TokenType::_BytesConstant:
      case TokenType::_UnicodeConstant: // probably a docstring
        if (this->token_at(this->token_num + 1).type == TokenType::_Newline) {
          this->advance_token();
          break;
        }
      case TokenType::_Dynamic: // expect a generic statement
      case TokenType::_OpenParen:
      	ret.emplace_back(this->parse_simple_statement(line_end_offset));
        break;

      case TokenType::Del: // DelStatement
        this->advance_token();
        ret.emplace_back(new DeleteStatement(
            this->parse_expression_tuple(line_end_offset, true), offset));
        break;

      case TokenType::Pass: // PassStatement
        this->advance_token();
      	ret.emplace_back(new PassStatement(offset));
        break;

      case TokenType::Break: // BreakStatement
        this->advance_token();
      	ret.emplace_back(new BreakStatement(offset));
        break;

      case TokenType::Continue: // ContinueStatement
        this->advance_token();
        ret.emplace_back(new ContinueStatement(offset));
        break;

      case TokenType::Return: { // ReturnStatement; expect an optional expression
        this->advance_token();

        shared_ptr<Expression> value;
        if (this->head_token().type != TokenType::_Newline) {
          value = this->parse_expression_tuple(line_end_offset);
        }
        ret.emplace_back(new ReturnStatement(value, offset));
        break;
      }

      case TokenType::Raise: { // RaiseStatement; expect an optional expression
        this->advance_token();

        shared_ptr<Expression> type, value, traceback;
        if (this->head_token().type != TokenType::_Newline) {
          auto exprs = this->parse_expression_list(line_end_offset);
          if (exprs.size() > 0) {
            type = exprs[0];
          }
          if (exprs.size() > 1) {
            value = exprs[1];
          }
          if (exprs.size() > 2) {
            traceback = exprs[2];
          }
          this->expect_condition(exprs.size() <= 3, ParseError::TooManyArguments,
              "too many arguments to raise statement");
        }
        ret.emplace_back(new RaiseStatement(type, value, traceback, offset));
        break;
      }

      case TokenType::Import: { // ImportStatement
        this->advance_token();

        // Import _Dynamic [As _Dynamic][, _Dynamic [As _Dynamic], ...]
        unordered_map<string, string> modules;
        while (this->head_token().type != TokenType::_Newline) {
          this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
              "expected name following import keyword");
          const string& name = this->head_token().string_data;
          this->advance_token();
          if (this->head_token().type == TokenType::As) {
            this->advance_token();
            this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
                "expected name following \'as\'");
            const string& rename = this->head_token().string_data;
            this->advance_token();
            modules.emplace(name, rename);
          } else {
            modules.emplace(name, name);
          }

          if (this->head_token().type == TokenType::_Comma) {
            this->advance_token();
          }
        }

        ret.emplace_back(new ImportStatement(move(modules),
            unordered_map<string, string>(), false, offset));
        break;
      }

      case TokenType::From: { // ImportStatement
        this->advance_token();

        // From _Dynamic Import *
        // From _Dynamic Import _Dynamic [As _Dynamic][, _Dynamic [As _Dynamic], ...]

        // read the module name (there should be only one)
        this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
            "expected name following \'from\'");
        const string& module = this->head_token().string_data;
        unordered_map<string, string> modules;
        modules.emplace(module, module);
        this->advance_token();

        // followed by "import"
        this->expect_token_type(TokenType::Import, ParseError::SyntaxError,
            "expected \'import\' after module name");
        this->advance_token();
        this->expect_condition(this->head_token().type != TokenType::_Newline,
            ParseError::SyntaxError, "expected something after from...import");

        // if it's a *, don't expect anything else
        unordered_map<string, string> names;
        if (this->head_token().type == TokenType::_Asterisk) {
          this->advance_token();

        // otherwise it's a list of symbols with optional renames
        } else {
          while (this->head_token().type != TokenType::_Newline) {
            this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
                "expected name for attribute import");
            const string& name = this->head_token().string_data;
            this->advance_token();
            if (this->head_token().type == TokenType::As) {
              this->advance_token();
              this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
                  "expected name following \'as\' for attribute import");
              const string& rename = this->head_token().string_data;
              this->advance_token();
              names.emplace(name, rename);
            } else {
              names.emplace(name, name);
            }

            if (this->head_token().type == TokenType::_Comma) {
              this->advance_token();
            }
          }
        }
        ret.emplace_back(new ImportStatement(move(modules), move(names),
            names.empty(), offset));
        break;
      }

      case TokenType::Def: { // FunctionDefinition
        this->advance_token();

        // read the name
        this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
            "expected name for function definition");
        string name = this->head_token().string_data;
        this->advance_token();

        // open paren...
        this->expect_token_type(TokenType::_OpenParen, ParseError::SyntaxError,
            "expected open parenthesis after function name");
        this->advance_token();

        // parse the args
        ssize_t args_end_offset = this->find_bracketed_end(TokenType::_CloseParen,
            end_offset);
        this->expect_condition(args_end_offset >= 0, ParseError::BracketingError);
        auto args = this->parse_function_argument_definition(args_end_offset);
        this->expect_offset(args_end_offset, ParseError::IncompleteParsing,
            "function argspec is incomplete");

        // close paren
        this->expect_token_type(TokenType::_CloseParen, ParseError::SyntaxError,
            "expected close parenthesis at end of argument list");
        this->advance_token();

        auto items = this->parse_suite_from_colon(end_offset);

        ret.emplace_back(new FunctionDefinition(move(decorator_stack), name,
            move(args), move(items), offset));
        newline_expected = false;
        break;
      }

      case TokenType::Global: // GlobalStatement; expect a list of _Dynamics
        this->advance_token();
        ret.emplace_back(new GlobalStatement(this->parse_dynamic_list(),
            offset));
        break;

      case TokenType::Exec: { // ExecStatement; expect an expression, or two or three
        this->advance_token();
        auto exprs = this->parse_expression_list(line_end_offset);
        shared_ptr<Expression> code, globals, locals;
        if (exprs.size() > 0) {
          code = exprs[0];
        }
        if (exprs.size() > 1) {
          globals = exprs[1];
        }
        if (exprs.size() > 2) {
          locals = exprs[2];
        }
        this->expect_condition(exprs.size() <= 3, ParseError::TooManyArguments,
            "too many arguments to exec statement");
        ret.emplace_back(new ExecStatement(code, globals, locals, offset));
        break;
      }

      case TokenType::Assert: { // AssertStatement; expect an expression, or two
        this->advance_token();
        auto exprs = this->parse_expression_list(line_end_offset);
        shared_ptr<Expression> check, failure_message;
        if (exprs.size() > 0) {
          check = exprs[0];
        }
        if (exprs.size() > 1) {
          failure_message = exprs[1];
        }
        this->expect_condition(exprs.size() <= 2, ParseError::TooManyArguments,
            "too many arguments to assert statement");
        ret.emplace_back(new AssertStatement(check, failure_message, offset));
        break;
      }

      case TokenType::If: { // IfStatement; expect an expression, colon, then suite
        clear();
        this->advance_token();

        // parse the expression
        ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, end_offset);
        this->expect_condition(colon_offset >= 0, ParseError::SyntaxError,
            "expected colon after if statement");
        auto check = this->parse_expression(colon_offset);
        this->expect_offset(colon_offset, ParseError::IncompleteParsing,
            "if expression is incomplete");

        auto items = this->parse_suite_from_colon(end_offset);

        // we'll fill in the elifs and else later
        prev_if.reset(new IfStatement(check, move(items),
            vector<shared_ptr<ElifStatement>>(), shared_ptr<ElseStatement>(),
            offset));
        ret.emplace_back(prev_if);
        newline_expected = false;
        should_clear_local = false;
        break;
      }

      case TokenType::Else: { // ElseStatement; expect a colon, then a suite
        expect_else();

        this->advance_token();
        this->expect_token_type(TokenType::_Colon, ParseError::SyntaxError,
            "expected colon after \'else\'");

        auto items = this->parse_suite_from_colon(end_offset);

        if (prev_if) {
          prev_if->else_suite.reset(new ElseStatement(move(items), offset));
        } else if (prev_for) {
          prev_for->else_suite.reset(new ElseStatement(move(items), offset));
        } else if (prev_while) {
          prev_while->else_suite.reset(new ElseStatement(move(items), offset));
        } else if (prev_try) {
          prev_try->else_suite.reset(new ElseStatement(move(items), offset));
          should_clear_local = false;
        } else {
          this->expect_condition(false, ParseError::SyntaxError,
              "else block not after if/for/while/try");
        }

        newline_expected = false;
        break;
      }

      case TokenType::Elif: { // ElifStatement; expect an expression, colon, then suite
        expect(false, true, false, false, false); // if-statement only

        this->advance_token();

        // parse the expression
        ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, end_offset);
        this->expect_condition(colon_offset >= 0, ParseError::SyntaxError,
            "expected colon after elif statement");
        auto check = this->parse_expression(colon_offset);
        this->expect_offset(colon_offset, ParseError::IncompleteParsing,
            "elif expression is incomplete");

        auto items = this->parse_suite_from_colon(end_offset);

        prev_if->elifs.emplace_back(new ElifStatement(check, move(items), offset));
        newline_expected = false;
        should_clear_local = false;
        break;
      }

      case TokenType::While: { // WhileStatement; expect an expression, colon, then suite
        clear();
        this->advance_token();

        // parse the expression
        ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, end_offset);
        this->expect_condition(colon_offset >= 0, ParseError::SyntaxError,
            "expected colon after while statement");
        auto condition = this->parse_expression(colon_offset);
        this->expect_offset(colon_offset, ParseError::IncompleteParsing,
            "while expression is incomplete");

        auto items = this->parse_suite_from_colon(end_offset);

        // we'll fill in the else clause later
        prev_while.reset(new WhileStatement(condition, move(items), NULL, offset));
        ret.emplace_back(prev_while);
        newline_expected = false;
        should_clear_local = false;
        break;
      }

      case TokenType::For: { // ForStatement; expect _Dynamic list, In, expr, colon, suite
        clear();
        this->advance_token();

        // parse the unpacking information
        ssize_t in_offset = this->find_bracketed_end(TokenType::In, end_offset);
        this->expect_condition(in_offset > 0, ParseError::SyntaxError,
            "expected \'in\' after \'for\'");
        auto variable = this->parse_expression_tuple(in_offset, true);

        // now the 'in'
        this->expect_token_type(TokenType::In, ParseError::SyntaxError,
            "expected \'in\' after \'for\' unpacking");
        this->advance_token();

        // parse the expressions
        ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, end_offset);
        this->expect_condition(colon_offset >= 0, ParseError::SyntaxError,
            "expected colon after \'for\'");
        auto collection = this->parse_expression_tuple(colon_offset);
        this->expect_offset(colon_offset, ParseError::IncompleteParsing,
            "for expression list is incomplete");

        auto items = this->parse_suite_from_colon(end_offset);

        // we'll fill in the else clause later
        prev_for.reset(new ForStatement(variable, collection, move(items), NULL, offset));
        ret.emplace_back(prev_for);
        newline_expected = false;
        should_clear_local = false;
        break;
      }

      case TokenType::Try: {  // TryStatement; expect a colon, then suite
        clear();
        this->advance_token();
        this->expect_token_type(TokenType::_Colon, ParseError::SyntaxError,
            "expected colon after \'try\'");

        auto items = this->parse_suite_from_colon(end_offset);

        prev_try.reset(new TryStatement(move(items),
            vector<shared_ptr<ExceptStatement>>(), shared_ptr<ElseStatement>(),
            shared_ptr<FinallyStatement>(), offset));
        ret.emplace_back(prev_try);
        newline_expected = false;
        should_clear_local = false;
        break;
      }

      case TokenType::Except: { // ExceptStatement
        expect(false, false, false, false, true); // try-statement only

        this->advance_token();

        ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, end_offset);
        this->expect_condition(colon_offset >= 0, ParseError::SyntaxError,
            "expected colon after \'except\'");

        // check if there's an As or _Comma before the _Colon
        static const vector<TokenType> token_types = {TokenType::_Comma, TokenType::As};
        auto find_ret = this->find_bracketed_any(token_types, colon_offset);
        if ((find_ret.first < 0) || (find_ret.first > colon_offset)) {
          find_ret.first = colon_offset;
        }

        shared_ptr<Expression> types;
        if (find_ret.first != this->token_num) {
          types = this->parse_expression(find_ret.first);
          this->expect_offset(find_ret.first, ParseError::IncompleteParsing,
              "exception expression is incomplete");
          if (find_ret.first != colon_offset) {
            this->advance_token();
          }
        }

        string name;
        if (colon_offset != this->token_num) {
          this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
              "expected name after \'as\' or comma");
          name = this->head_token().string_data;
          this->advance_token();
          this->expect_offset(colon_offset, ParseError::SyntaxError,
              "expected colon at end of except statement");
        }

        auto items = this->parse_suite_from_colon(end_offset);

        prev_try->excepts.emplace_back(new ExceptStatement(types, name, move(items), offset));
        newline_expected = false;
        should_clear_local = false;
        break;
      }

      case TokenType::Finally: { // FinallyStatement; expect a colon, then quite
        expect(false, false, false, false, true); // try-statement only

        this->advance_token();
        this->expect_token_type(TokenType::_Colon, ParseError::SyntaxError,
            "expected colon after \'finally\'");

        auto items = this->parse_suite_from_colon(end_offset);

        // TODO: Make sure none of these are already set when we set them
        prev_try->finally_suite.reset(new FinallyStatement(move(items), offset));
        newline_expected = false;
        break;
      }

      case TokenType::Class: { // ClassDefinition
        this->advance_token();
        this->expect_token_type(TokenType::_Dynamic, ParseError::SyntaxError,
            "expected class name");

        string name = this->head_token().string_data;
        this->advance_token();

        vector<shared_ptr<Expression>> parent_types;
        if (this->head_token().type == TokenType::_OpenParen) {
          this->advance_token();
          ssize_t close_paren_offset = this->find_bracketed_end(TokenType::_CloseParen,
              line_end_offset);
          this->expect_condition((close_paren_offset >= 0) &&
              (close_paren_offset < line_end_offset), ParseError::SyntaxError,
              "expected close parenthesis after class name");

          parent_types = this->parse_expression_list(close_paren_offset);
          this->expect_offset(close_paren_offset, ParseError::IncompleteParsing,
              "class parent type list is incomplete");
          this->advance_token();
        }

        auto items = this->parse_suite_from_colon(end_offset);

        ret.emplace_back(new ClassDefinition(move(decorator_stack), name,
            move(parent_types), move(items), offset));
        newline_expected = false;
        break;
      }

      case TokenType::With: { // WithStatement
        this->advance_token();

        ssize_t colon_offset = this->find_bracketed_end(TokenType::_Colon, end_offset);
        this->expect_condition(colon_offset >= 0, ParseError::SyntaxError,
            "expected colon after \'with\'");

        vector<pair<shared_ptr<Expression>, string>> item_to_name;
        while (this->head_token().type != TokenType::_Colon) {
          // check if there's a _Comma before the _Colon
          ssize_t comma_offset = this->find_bracketed_end(TokenType::_Comma, colon_offset);
          if ((comma_offset < 0) || (comma_offset > colon_offset)) {
            comma_offset = colon_offset;
          }

          ssize_t as_offset = find_bracketed_end(TokenType::As, comma_offset);
          if ((as_offset < 0) || (as_offset > comma_offset)) {
            as_offset = comma_offset;
          }

          shared_ptr<Expression> expr = this->parse_expression(as_offset);
          this->expect_offset(as_offset, ParseError::IncompleteParsing,
              "with context expression is incomplete");

          if (as_offset != comma_offset) {
            this->advance_token();
            this->expect_token_type(TokenType::_Dynamic, ParseError::IncompleteParsing,
                "excess tokens after \'as\'");
            item_to_name.emplace_back(make_pair(expr, this->head_token().string_data));
            this->advance_token();

          } else {
            item_to_name.emplace_back(make_pair(expr, ""));
          }

          this->expect_offset(comma_offset, ParseError::IncompleteParsing,
              "with context definition is incomplete");

          if (comma_offset != colon_offset) {
            this->expect_token_type(TokenType::_Comma, ParseError::SyntaxError,
                "expected comma here");
            this->advance_token();
          }
        }

        auto items = this->parse_suite_from_colon(end_offset);

        ret.emplace_back(new WithStatement(move(item_to_name), move(items), offset));
        newline_expected = false;
        should_clear_local = false;
        break;
      }

      case TokenType::Yield: { // YieldStatement
        this->advance_token();

        bool from = this->head_token().type == TokenType::From;
        if (from) {
          this->advance_token();
        }

        shared_ptr<Expression> expr;
        if (this->head_token().type != TokenType::_Newline) {
          expr = this->parse_expression(line_end_offset);
        }

        ret.emplace_back(new YieldStatement(expr, from, offset));
        break;
      }

      case TokenType::_At: // Decorator
        this->advance_token();
        decorator_stack.emplace_back(this->parse_expression(
            line_end_offset));
        should_clear_local = false;
        break;

      case TokenType::_Indent: // this should have been handled in another case
      case TokenType::_Unindent:
        this->expect_condition(false, ParseError::InvalidIndentationChange,
            "indent encountered out of line");

      default:
        this->expect_condition(false, ParseError::InvalidStartingTokenType,
            "line starts with an invalid token type: " + this->head_token().str());
    }

    // here, we expect to be at either EOF or a newline token
    if (newline_expected) {
      this->expect_token_type(TokenType::_Newline, ParseError::ExtraDataAfterLine);
      this->advance_token();
    }
    if (should_clear_local) {
      clear();
    }
  }

  this->expect_condition(decorator_stack.size() == 0, ParseError::SyntaxError,
      "decorator stack was not empty at end of compound statement");

  return ret;
}
