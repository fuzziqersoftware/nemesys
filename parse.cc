#include "parse.hh"

// PythonAST

PythonAST::PythonAST() : root(), error(NoParseError), failure_offset(-1), failure_explanation() {}

// parser state & utility functions

struct ParserState {
	const TokenStream* stream;
	int token_num;
	PythonAST* ast;

  void set_parse_error(ParseError error, const string explanation = "") {
    if (ast->error == NoParseError) {
      ast->error = error;
      ast->failure_offset = token_num;
      ast->failure_explanation = explanation;
    }
  }

  inline bool error() {
    return ast->error;
  }

  inline const InputToken& head_token() {
    return stream->tokens[token_num];
  }

  inline const InputToken& token_at(int offset) {
    return stream->tokens[offset];
  }

  inline void advance_token() {
    token_num++;
  }
};

bool expect_condition(ParserState* st, bool condition, ParseError error, const string explanation = "") {
  if (!condition) {
    st->set_parse_error(error, explanation);
    return false;
  }
  return true;
}

bool expect_token_type(ParserState* st, TokenType type, ParseError error, const string explanation = "") {
  return expect_condition(st, st->head_token().type == type, error, explanation);
}

bool expect_offset(ParserState* st, int offset, ParseError error, const string explanation = "") {
  return expect_condition(st, st->token_num == offset, error, explanation);
}

void find_bracketed_any(ParserState* st, const TokenType* types, int num_types, int end_offset, int* token_offset, int* token_index, bool find_last = false) {
  // same as find_bracketed_end but can search for multiple token types.
  *token_offset = -1;
  *token_index = -1;

  int offset = st->token_num;
  vector<TokenType> open_stack;
  while ((offset < end_offset) && (st->error() == NoParseError)) {
    // check if it matches any of the given tokens
    if (open_stack.size() == 0) {
      for (int x = 0; x < num_types; x++) {
        if (st->stream->tokens[offset].type == types[x]) {
          *token_offset = offset;
          *token_index = x;
          if (!find_last)
            return;
        }
      }
    }

    if (open_stack.size() > 0 && open_stack.back() == st->stream->tokens[offset].type)
      open_stack.pop_back(); // expected close brace/bracket/whatever
    else if (is_open_bracket_token(st->stream->tokens[offset].type))
      open_stack.push_back(get_closing_bracket_token_type(st->stream->tokens[offset].type));
    else
      expect_condition(st, !token_requires_opener(st->token_at(offset).type), BracketingError, "found a closing token with no matching open token");

    offset++;
  }
}

int find_bracketed_end(ParserState* st, TokenType type, int end_offset, bool find_last = false) {
  int token_offset, token_index;
  find_bracketed_any(st, &type, 1, end_offset, &token_offset, &token_index, find_last);
  return token_offset;
}

vector<string> parse_dynamic_list(ParserState* st) {
  // imperfect comma-separated _Dynamic list parsing. some "features":
  // - the list can end with a comma not followed by a _Dynamic
  // - the _Dynamics don't have to be comma-separated
  // I think these "features" are ok though because I'm lazy.
  vector<string> result;
  expect_token_type(st, _Dynamic, InvalidDynamicList);
  while (!st->error() && (st->head_token().type == _Dynamic)) {
    result.push_back(st->head_token().string_data);
    st->advance_token();
    if (st->head_token().type == _Comma)
      st->advance_token();
  }
  return result;
}



// expression parsing

static shared_ptr<Expression> parse_expression(ParserState*, int);

static shared_ptr<Expression> parse_binary_operator(ParserState* st, int operator_offset, int end_offset, BinaryOperator oper) {

  shared_ptr<BinaryOperation> binary(new BinaryOperation());
  binary->oper = oper;
  binary->left = parse_expression(st, operator_offset);
  if (!expect_offset(st, operator_offset, IncompleteParsing))
    return shared_ptr<Expression>();
  st->advance_token();
  binary->right = parse_expression(st, end_offset);
  if (!expect_offset(st, end_offset, IncompleteParsing))
    return shared_ptr<Expression>();
  return binary;
}

static shared_ptr<Expression> parse_unary_operator(ParserState* st, int end_offset, UnaryOperator oper) {

  st->advance_token();
  shared_ptr<UnaryOperation> unary(new UnaryOperation());
  unary->oper = oper;
  unary->expr = parse_expression(st, end_offset);
  if (!expect_offset(st, end_offset, IncompleteParsing))
    return shared_ptr<Expression>();
  return unary;
}

static void parse_expression_list(ParserState* st, vector<shared_ptr<Expression> >& items, int end_offset) {
  while (st->token_num < end_offset && st->error() == NoParseError) {
    int comma_offset = find_bracketed_end(st, _Comma, end_offset);
    if (comma_offset < 0)
      comma_offset = end_offset;
    items.push_back(parse_expression(st, comma_offset));
    if (!expect_offset(st, comma_offset, IncompleteParsing))
      return;
    if (comma_offset < end_offset)
      st->advance_token();
  }
}

static void parse_dict_item_list(ParserState* st, vector<pair<shared_ptr<Expression>, shared_ptr<Expression> > >& items, int end_offset) {
  while (st->token_num < end_offset && st->error() == NoParseError) {
    int comma_offset = find_bracketed_end(st, _Comma, end_offset);
    if (comma_offset < 0)
      comma_offset = end_offset;
    int colon_offset = find_bracketed_end(st, _Colon, comma_offset);
    if (!expect_condition(st, colon_offset > 0 && colon_offset < comma_offset, IncompleteDictItem, "dict item does not contain a colon"))
      return;

    shared_ptr<Expression> key = parse_expression(st, colon_offset);
    if (!expect_offset(st, colon_offset, IncompleteParsing))
      return;
    st->advance_token();
    shared_ptr<Expression> value = parse_expression(st, comma_offset);
    if (!expect_offset(st, comma_offset, IncompleteParsing))
      return;
    if (comma_offset < end_offset)
      st->advance_token();

    items.push_back(make_pair(key, value));
  }
}

static void parse_function_argument_definition(ParserState* st, vector<shared_ptr<ArgumentDefinition> >& args, int end_offset) {
  while (st->token_num < end_offset) {
    int comma_offset = find_bracketed_end(st, _Comma, end_offset);
    if (comma_offset == -1)
      comma_offset = end_offset;

    // if there's a * or **, it's a *args or **kwargs. expect a _Dynamic followed by maybe a _Comma
    ArgumentMode mode = DefaultArgMode;
    if (st->head_token().type == _Asterisk)
      mode = ArgListMode;
    if (st->head_token().type == _DoubleAsterisk)
      mode = KeywordArgListMode;

    if (mode != DefaultArgMode) {
      st->advance_token();
      if (!expect_token_type(st, _Dynamic, SyntaxError))
        return;
      args.push_back(shared_ptr<ArgumentDefinition>(new ArgumentDefinition(st->head_token().string_data, shared_ptr<Expression>(), mode)));
      st->advance_token();

    // else it's a normal arg
    } else {
      string name = st->head_token().string_data;
      st->advance_token();

      // if there's a top-level =, then it's a kwarg
      shared_ptr<Expression> default_value;
      if (st->head_token().type == _Equals) {
        st->advance_token();
        default_value = parse_expression(st, comma_offset);
      }

      args.push_back(shared_ptr<ArgumentDefinition>(new ArgumentDefinition(name, default_value, mode)));
    }

    if (comma_offset < end_offset) {
      if (!expect_token_type(st, _Comma, IncompleteParsing))
        return;
      st->advance_token(); // skip comma
    }
  }
}

static void parse_function_call_arguments(ParserState* st, vector<shared_ptr<ArgumentDefinition> >& args, int end_offset) {
  // TODO: reduce code duplication with this function and the ebove
  while (st->token_num < end_offset) {
    int comma_offset = find_bracketed_end(st, _Comma, end_offset);
    if (comma_offset == -1)
      comma_offset = end_offset;

    // if there's a * or **, it's a *args or **kwargs; change the arg mode
    ArgumentMode mode = DefaultArgMode;
    if (st->head_token().type == _Asterisk) {
      mode = ArgListMode;
      st->advance_token();
    }
    if (st->head_token().type == _DoubleAsterisk) {
      mode = KeywordArgListMode;
      st->advance_token();
    }

    // if there's a top-level =, then it's a kwarg
    string name;
    int equals_offset = find_bracketed_end(st, _Equals, comma_offset);
    if (equals_offset >= 0) {
      if (!expect_condition(st, (mode == DefaultArgMode) && (equals_offset == st->token_num + 1), SyntaxError, "found =, but name does not immediately precede it"))
        return;
      if (!expect_token_type(st, _Dynamic, SyntaxError))
        return;
      name = st->head_token().string_data;
      st->advance_token();
      if (!expect_token_type(st, _Equals, SyntaxError))
        return;
      st->advance_token();
    }

    shared_ptr<Expression> default_value = parse_expression(st, comma_offset);
    args.push_back(shared_ptr<ArgumentDefinition>(new ArgumentDefinition(name, default_value, mode)));

    if (comma_offset < end_offset) {
      if (!expect_token_type(st, _Comma, IncompleteParsing))
        return;
      st->advance_token(); // skip comma
    }
  }
}

static shared_ptr<UnpackingFormat> parse_unpacking_format(ParserState* st, int end_offset) {
  // if there are no commas at all, it's a single variable
  int comma_offset = find_bracketed_end(st, _Comma, end_offset);
  if (comma_offset == -1) {
    if (!expect_condition(st, (st->head_token().type == _Dynamic) && (end_offset == st->token_num + 1), SyntaxError, "unpacking format has a non-dynamic token or too many tokens"))
      return shared_ptr<UnpackingFormat>();
    string var = st->head_token().string_data;
    st->advance_token();
    return shared_ptr<UnpackingVariable>(new UnpackingVariable(var));
  }

  // if we get here, then it's a tuple
  shared_ptr<UnpackingTuple> tuple(new UnpackingTuple());
  while (st->token_num < end_offset) {
    comma_offset = find_bracketed_end(st, _Comma, end_offset);
    if (comma_offset == -1)
      comma_offset = end_offset;

    if (st->head_token().type == _OpenParen) {
      st->advance_token();
      if (!expect_condition(st, st->token_at(comma_offset - 1).type == _CloseParen, BracketingError, "found a tuple but it does not cover the entire region"))
        shared_ptr<UnpackingFormat>();
      tuple->objects.push_back(parse_unpacking_format(st, comma_offset - 1));
      if (!expect_offset(st, comma_offset - 1, IncompleteParsing))
        return shared_ptr<UnpackingFormat>();
    } else {
      if (!expect_condition(st, st->token_num == comma_offset - 1, SyntaxError, "multiple tokens before comma"))
        return shared_ptr<UnpackingFormat>();
      if (!expect_token_type(st, _Dynamic, SyntaxError))
        return shared_ptr<UnpackingFormat>();
      tuple->objects.push_back(shared_ptr<UnpackingVariable>(new UnpackingVariable(st->head_token().string_data)));
    }

    st->advance_token(); // skip close paren
    if (comma_offset < end_offset) {
      if (!expect_token_type(st, _Comma, IncompleteParsing))
        return shared_ptr<UnpackingFormat>();
      st->advance_token(); // skip comma
    }
  }
  return tuple;
}

static shared_ptr<Expression> parse_expression(ParserState* st, int end_offset) {

  // 16. lambda
  if (st->head_token().type == Lambda) {
    shared_ptr<LambdaDefinition> lambda(new LambdaDefinition());
    st->advance_token();

    int colon_offset = find_bracketed_end(st, _Colon, end_offset);
    if (!expect_condition(st, colon_offset >= 0 && colon_offset < end_offset, IncompleteLambdaDefinition, "lambda has no colon"))
      return shared_ptr<Expression>();

    parse_function_argument_definition(st, lambda->args, colon_offset);
    if (!expect_offset(st, colon_offset, IncompleteParsing))
      return shared_ptr<Expression>();
    st->advance_token();
    lambda->result = parse_expression(st, end_offset);
    if (!expect_offset(st, end_offset, IncompleteParsing))
      return shared_ptr<Expression>();
    return lambda;
  }

  // 15. x if y else z
  int if_offset = find_bracketed_end(st, If, end_offset, true);
  if (if_offset > st->token_num && if_offset < end_offset) {
    int else_offset = find_bracketed_end(st, Else, end_offset, true);
    if (!expect_condition(st, else_offset > if_offset && else_offset < end_offset, IncompleteTernaryOperator))
      return shared_ptr<Expression>();

    shared_ptr<TernaryOperation> ternary(new TernaryOperation());
    ternary->oper = IfElseOperator;
    ternary->left = parse_expression(st, if_offset);
    if (!expect_offset(st, if_offset, IncompleteParsing))
      return shared_ptr<Expression>();
    st->advance_token();
    ternary->center = parse_expression(st, else_offset);
    if (!expect_offset(st, else_offset, IncompleteParsing))
      return shared_ptr<Expression>();
    st->advance_token();
    ternary->right = parse_expression(st, end_offset);
    if (!expect_offset(st, end_offset, IncompleteParsing))
      return shared_ptr<Expression>();
    return ternary;
  }

  // 14. or
  int oper_offset = find_bracketed_end(st, Or, end_offset, true);
  if (oper_offset > st->token_num && oper_offset < end_offset)
    return parse_binary_operator(st, oper_offset, end_offset, LogicalOrOperator);

  // 13. and
  oper_offset = find_bracketed_end(st, And, end_offset, true);
  if (oper_offset > st->token_num && oper_offset < end_offset)
    return parse_binary_operator(st, oper_offset, end_offset, LogicalAndOperator);

  // 12. not
  if (st->head_token().type == Not)
    return parse_unary_operator(st, end_offset, LogicalNotOperator);

  // 11. in, not in, is, is not, <, <=, >, >=, <>, !=, ==
  static const BinaryOperator operators_level11[]  = {InOperator, NotInOperator, IsOperator, IsNotOperator, LessThanOperator, LessOrEqualOperator, GreaterThanOperator, GreaterOrEqualOperator, NotEqualOperator, EqualityOperator};
  static const TokenType operator_tokens_level11[] = {In,         NotIn,         Is,         IsNot,         _LessThan,        _LessOrEqual,        _GreaterThan,        _GreaterOrEqual,        _NotEqual,        _Equality};
  int oper_index;
  find_bracketed_any(st, operator_tokens_level11, sizeof(operator_tokens_level11) / sizeof(operator_tokens_level11[0]), end_offset, &oper_offset, &oper_index, true);
  if (oper_offset > st->token_num && oper_offset < end_offset)
    return parse_binary_operator(st, oper_offset, end_offset, operators_level11[oper_index]);

  // 10. |
  oper_offset = find_bracketed_end(st, _Or, end_offset, true);
  if (oper_offset > st->token_num && oper_offset < end_offset)
    return parse_binary_operator(st, oper_offset, end_offset, OrOperator);

  // 9. ^
  oper_offset = find_bracketed_end(st, _Xor, end_offset, true);
  if (oper_offset > st->token_num && oper_offset < end_offset)
    return parse_binary_operator(st, oper_offset, end_offset, XorOperator);

  // 8. &
  oper_offset = find_bracketed_end(st, _And, end_offset, true);
  if (oper_offset > st->token_num && oper_offset < end_offset)
    return parse_binary_operator(st, oper_offset, end_offset, AndOperator);

  // 7. <<, >>
  static const BinaryOperator operators_level7[]  = {LeftShiftOperator, RightShiftOperator};
  static const TokenType operator_tokens_level7[] = {_LeftShift, _RightShift};
  find_bracketed_any(st, operator_tokens_level7, sizeof(operator_tokens_level7) / sizeof(operator_tokens_level7[0]), end_offset, &oper_offset, &oper_index, true);
  if (oper_offset > st->token_num && oper_offset < end_offset)
    return parse_binary_operator(st, oper_offset, end_offset, operators_level7[oper_index]);

  // 6. +, -
  // we assume these operators are binary unless there's (a) another operator or (b) nothing on the left
  static const BinaryOperator operators_level6[]  = {AdditionOperator, SubtractionOperator};
  static const TokenType operator_tokens_level6[] = {_Plus, _Minus};
  find_bracketed_any(st, operator_tokens_level6, sizeof(operator_tokens_level6) / sizeof(operator_tokens_level6[0]), end_offset, &oper_offset, &oper_index, true);
  if (oper_offset > st->token_num && oper_offset < end_offset && !is_operator_token(st->token_at(oper_offset - 1).type)) {
    return parse_binary_operator(st, oper_offset, end_offset, operators_level6[oper_index]);
  }

  // 5. *, /, //, %
  static const BinaryOperator operators_level5[]  = {MultiplicationOperator, DivisionOperator, IntegerDivisionOperator, ModulusOperator};
  static const TokenType operator_tokens_level5[] = {_Asterisk, _Slash, _DoubleSlash, _Percent};
  find_bracketed_any(st, operator_tokens_level5, sizeof(operator_tokens_level5) / sizeof(operator_tokens_level5[0]), end_offset, &oper_offset, &oper_index, true);
  if (oper_offset > st->token_num && oper_offset < end_offset) {
    return parse_binary_operator(st, oper_offset, end_offset, operators_level5[oper_index]);
  }

  // 4. +x, -x, ~x
  if (st->head_token().type == _Plus)
    return parse_unary_operator(st, end_offset, PositiveOperator);
  if (st->head_token().type == _Minus)
    return parse_unary_operator(st, end_offset, NegativeOperator);
  if (st->head_token().type == _Tilde)
    return parse_unary_operator(st, end_offset, NotOperator);

  // 3. ** (note: The power operator ** binds less tightly than an arithmetic or bitwise unary operator on its right, that is, 2**-1 is 0.5.)
  oper_offset = find_bracketed_end(st, _DoubleAsterisk, end_offset, true);
  if (oper_offset > st->token_num && oper_offset < end_offset)
    return parse_binary_operator(st, oper_offset, end_offset, ExponentiationOperator);

  // 2. x[y], x[y:z], x(y, z, ...), x.y
  // TODO: maybe we should pick the latest one instead? the outermost "call" should happen first right?
  int bracket_offset = find_bracketed_end(st, _OpenBracket, end_offset, true);
  int paren_offset = find_bracketed_end(st, _OpenParen, end_offset, true);
  int dot_offset = find_bracketed_end(st, _Dot, end_offset, true);
  int effective_offset = max(bracket_offset, max(dot_offset, paren_offset));

  if (effective_offset > st->token_num) {

    // array index / slice
    if (effective_offset == bracket_offset) {

      // parse array expr first
      shared_ptr<Expression> array = parse_expression(st, bracket_offset);
      if (!expect_offset(st, bracket_offset, IncompleteParsing))
        return shared_ptr<Expression>();
      st->advance_token();

      // find the end of the index
      int bracket_end_offset = find_bracketed_end(st, _CloseBracket, end_offset);
      if (!expect_condition(st, bracket_end_offset > bracket_offset && bracket_end_offset < end_offset, BracketingError))
        return shared_ptr<Expression>();

      // if there's a colon, it's a slice
      int colon_offset = find_bracketed_end(st, _Colon, bracket_end_offset);
      if (colon_offset > bracket_offset && colon_offset < bracket_end_offset) {
        shared_ptr<ArraySlice> slice(new ArraySlice());
        if (colon_offset > bracket_offset + 1)
          slice->slice_left = parse_expression(st, colon_offset);
        if (!expect_offset(st, colon_offset, IncompleteParsing))
          return shared_ptr<Expression>();
        st->advance_token();
        if (colon_offset < bracket_end_offset - 1)
          slice->slice_right = parse_expression(st, bracket_end_offset);
        if (!expect_offset(st, bracket_end_offset, IncompleteParsing))
          return shared_ptr<Expression>();
        st->advance_token();
        slice->array = array;
        return slice;

      // else it's just a normal array index
      } else {
        shared_ptr<ArrayIndex> index(new ArrayIndex());
        index->index = parse_expression(st, bracket_end_offset);
        if (!expect_offset(st, bracket_end_offset, IncompleteParsing))
          return shared_ptr<Expression>();
        st->advance_token();
        index->array = array;
        return index;
      }

    // argument list
    } else if (effective_offset == paren_offset) {
      shared_ptr<FunctionCall> call(new FunctionCall());

      call->function = parse_expression(st, paren_offset);
      if (!expect_offset(st, paren_offset, IncompleteParsing))
        return shared_ptr<Expression>();
      st->advance_token();

      int paren_end_offset = find_bracketed_end(st, _CloseParen, end_offset);
      if (!expect_condition(st, paren_end_offset > paren_offset && paren_end_offset < end_offset, BracketingError))
        return shared_ptr<Expression>();

      parse_function_call_arguments(st, call->args, paren_end_offset);
      if (!expect_offset(st, paren_end_offset, IncompleteParsing))
        return shared_ptr<Expression>();
      st->advance_token();
      return call;

    // attribute lookup
    } else if (effective_offset == dot_offset) {
      shared_ptr<AttributeLookup> attr(new AttributeLookup());
      attr->left = parse_expression(st, dot_offset);
      if (!expect_offset(st, dot_offset, IncompleteParsing))
        return shared_ptr<Expression>();
      st->advance_token();
      attr->right = parse_expression(st, end_offset);
      if (!expect_offset(st, end_offset, IncompleteParsing))
        return shared_ptr<Expression>();
      return attr;
    }
  }

  // 1. (expressions...), [expressions...], {key: value...}
  int brace_offset = find_bracketed_end(st, _OpenBrace, end_offset, true);

  // list constructor/comprehension
  if (bracket_offset == st->token_num) {
    if (!expect_condition(st, st->token_at(end_offset - 1).type == _CloseBracket, IncompleteParsing))
      return shared_ptr<Expression>();
    st->advance_token();

    // if it's [], then it's an empty list
    if (st->token_num == end_offset - 1) {
      st->advance_token();
      return shared_ptr<ListConstructor>(new ListConstructor());
    }

    // if there's a top-level 'for' and 'in', assume it's a comprehension
    int for_offset = find_bracketed_end(st, For, end_offset - 1);
    if (for_offset >= 0 && for_offset < end_offset) {
      shared_ptr<ListComprehension> comp(new ListComprehension());
      comp->item_pattern = parse_expression(st, for_offset);
      if (!expect_offset(st, for_offset, IncompleteParsing))
        return comp;
      st->advance_token();

      int in_offset = find_bracketed_end(st, In, end_offset - 1);
      if (!expect_condition(st, in_offset > for_offset && in_offset < end_offset, IncompleteGeneratorExpression))
        return comp;
      comp->variables = parse_unpacking_format(st, in_offset);
      if (!expect_offset(st, in_offset, IncompleteParsing))
        return comp;
      st->advance_token();

      int expr_end_offset = end_offset - 1;
      int if_offset = find_bracketed_end(st, If, end_offset - 1);
      if (if_offset > in_offset && in_offset < end_offset)
        expr_end_offset = if_offset;

      comp->source_data = parse_expression(st, expr_end_offset);
      if (expect_offset(st, expr_end_offset, IncompleteParsing))
        st->advance_token();

      if (if_offset > in_offset && in_offset < end_offset) {
        comp->if_expr = parse_expression(st, end_offset - 1);
        if (expect_offset(st, end_offset - 1, IncompleteParsing))
          st->advance_token();
      }
      return comp;
    }

    // parse the list values
    shared_ptr<ListConstructor> list(new ListConstructor());
    parse_expression_list(st, list->items, end_offset - 1);
    if (expect_offset(st, end_offset - 1, IncompleteParsing))
      st->advance_token();
    return list;

  // dict/set constructor/comprehension
  } else if (brace_offset == st->token_num) {
    if (!expect_condition(st, st->token_at(end_offset - 1).type == _CloseBrace, IncompleteParsing))
      return shared_ptr<Expression>();
    st->advance_token();

    // if it's {}, then it's an empty dict
    if (st->token_num == end_offset - 1) {
      st->advance_token();
      return shared_ptr<DictConstructor>(new DictConstructor());
    }

    // if there's a top-level : then it's a dict; otherwise it's a set
    int colon_offset = find_bracketed_end(st, _Colon, end_offset - 1);
    bool is_dict = (colon_offset >= 0 && colon_offset < end_offset);

    // if there's a top-level 'for' and 'in', assume it's a comprehension
    int for_offset = find_bracketed_end(st, For, end_offset - 1, true);
    if (for_offset >= 0 && for_offset < end_offset) {
      int in_offset = find_bracketed_end(st, In, end_offset - 1, true);
      if (!expect_condition(st, in_offset > for_offset && in_offset < end_offset, IncompleteGeneratorExpression))
        return shared_ptr<Expression>();

      shared_ptr<Expression> key_pattern, item_pattern, source_data, if_expr;
      shared_ptr<UnpackingFormat> variables;
      if (is_dict) {
        key_pattern = parse_expression(st, colon_offset);
        if (!expect_offset(st, colon_offset, IncompleteParsing))
          return shared_ptr<Expression>();
        st->advance_token();
      }
      item_pattern = parse_expression(st, for_offset);
      if (!expect_offset(st, for_offset, IncompleteParsing))
        return shared_ptr<Expression>();
      st->advance_token();

      variables = parse_unpacking_format(st, in_offset);
      if (!expect_offset(st, in_offset, IncompleteParsing))
        return shared_ptr<Expression>();
      st->advance_token();

      int expr_end_offset = end_offset - 1;
      int if_offset = find_bracketed_end(st, If, end_offset - 1);
      if (if_offset > in_offset && in_offset < end_offset)
        expr_end_offset = if_offset;

      source_data = parse_expression(st, expr_end_offset);
      if (expect_offset(st, expr_end_offset, IncompleteParsing))
        st->advance_token();

      if (if_offset > in_offset && in_offset < end_offset) {
        if_expr = parse_expression(st, end_offset - 1);
        if (expect_offset(st, end_offset - 1, IncompleteParsing))
          st->advance_token();
      }

      if (is_dict)
        return shared_ptr<DictComprehension>(new DictComprehension(key_pattern, item_pattern, variables, source_data, if_expr));
      return shared_ptr<SetComprehension>(new SetComprehension(item_pattern, variables, source_data, if_expr));
    }

    // else, it's just a simple constructor - parse the values
    if (is_dict) {
      shared_ptr<DictConstructor> dict(new DictConstructor());
      parse_dict_item_list(st, dict->items, end_offset - 1);
      if (expect_offset(st, end_offset - 1, IncompleteParsing))
        st->advance_token();
      return dict;
    } else {
      shared_ptr<SetConstructor> set_const(new SetConstructor());
      parse_expression_list(st, set_const->items, end_offset - 1);
      if (expect_offset(st, end_offset - 1, IncompleteParsing))
        st->advance_token();
      return set_const;
    }

  // tuple constructor
  } else if (paren_offset == st->token_num) {
    if (!expect_condition(st, st->token_at(end_offset - 1).type == _CloseParen, IncompleteParsing))
      return shared_ptr<Expression>();
    st->advance_token();

    // parse the tuple values
    shared_ptr<TupleConstructor> tuple(new TupleConstructor());
    parse_expression_list(st, tuple->items, end_offset - 1);
    if (expect_offset(st, end_offset - 1, IncompleteParsing))
      st->advance_token();
    return tuple;
  }

  // it's probably a constant if we get here
  if (st->token_num == end_offset - 1) {
    const InputToken& tok = st->head_token();
    st->advance_token();
    if (tok.type == _Integer)
      return shared_ptr<IntegerConstant>(new IntegerConstant(tok.int_data));
    if (tok.type == _Float)
      return shared_ptr<FloatingConstant>(new FloatingConstant(tok.float_data));
    if (tok.type == _StringConstant)
      return shared_ptr<StringConstant>(new StringConstant(tok.string_data));
    if (tok.type == _Dynamic) {
      if (!strcmp(tok.string_data.c_str(), "True"))
        return shared_ptr<TrueConstant>(new TrueConstant());
      if (!strcmp(tok.string_data.c_str(), "False"))
        return shared_ptr<FalseConstant>(new FalseConstant());
      if (!strcmp(tok.string_data.c_str(), "None"))
        return shared_ptr<NoneConstant>(new NoneConstant());
      return shared_ptr<VariableLookup>(new VariableLookup(tok.string_data));
    }
  }

  // uh-oh, we got something unparseable
  st->set_parse_error(IncompleteExpressionParsing, "no expression parsing rules matched");
  return shared_ptr<Expression>();
}



// statement parsing

struct LocalParserState {
  // parser state local to the current indentation level
  vector<shared_ptr<Expression> > decorator_stack;
  shared_ptr<IfStatement> prev_if;
  shared_ptr<ForStatement> prev_for;
  shared_ptr<WhileStatement> prev_while;
  shared_ptr<TryStatement> prev_try;

  bool expect(ParserState* st, bool has_decorators, bool has_if, bool has_for, bool has_while, bool has_try) {
    return expect_condition(st, has_decorators == !!decorator_stack.size(), SyntaxError) &&
        expect_condition(st, !has_if == !prev_if, SyntaxError) &&
        expect_condition(st, !has_for == !prev_for, SyntaxError) &&
        expect_condition(st, !has_while == !prev_while, SyntaxError) &&
        expect_condition(st, !has_try == !prev_try, SyntaxError);
  }

  bool expect_else(ParserState* st) {
    int num_valid = 0;
    num_valid += !!prev_if;
    num_valid += !!prev_for;
    num_valid += !!prev_while;
    num_valid += !!prev_try;
    return expect_condition(st, (num_valid == 1) && (decorator_stack.size() == 0), SyntaxError);
  }

  void clear() {
    decorator_stack.clear();
    prev_if = shared_ptr<IfStatement>();
    prev_for = shared_ptr<ForStatement>();
    prev_while = shared_ptr<WhileStatement>();
    prev_try = shared_ptr<TryStatement>();
  }
};

static void parse_compound_statement_suite(ParserState* st, shared_ptr<CompoundStatement> compound, int end_offset);

static void parse_suite_from_colon(ParserState* st, shared_ptr<CompoundStatement> i, int end_offset) {
  // colon, newline, indent
  if (!expect_token_type(st, _Colon, SyntaxError))
    return;
  st->advance_token();

  if (st->head_token().type == _Newline) {
    st->advance_token();
    if (!expect_token_type(st, _Indent, SyntaxError))
      return;
    st->advance_token();

    // parse the suite
    int suite_end_offset = find_bracketed_end(st, _Unindent, end_offset);
    if (!expect_condition(st, suite_end_offset >= 0, BracketingError))
      return;
    parse_compound_statement_suite(st, i, suite_end_offset);
    if (!expect_offset(st, suite_end_offset, IncompleteParsing))
      return;

    // better end with an _Unindent
    if (expect_token_type(st, _Unindent, SyntaxError))
      st->advance_token();

  } else {

    // parse the suite
    int suite_end_offset = find_bracketed_end(st, _Newline, end_offset);
    if (!expect_condition(st, suite_end_offset >= 0, BracketingError))
      return;
    parse_compound_statement_suite(st, i, suite_end_offset + 1);
    if (!expect_offset(st, suite_end_offset + 1, IncompleteParsing))
      return;
  }
}

#if 0

static void parse_equals_left(ParserState* st, int end_offset, shared_ptr<Expression>& array, shared_ptr<Expression>& index, shared_ptr<UnpackingFormat>& format) {

  // it's a setitem
  int bracket_offset = find_bracketed_end(st, _OpenBracket, end_offset);
  if (bracket_offset >= 0 && bracket_offset < end_offset) {
    format = shared_ptr<UnpackingFormat>();

    array = parse_expression(st, bracket_offset);
    if (!expect_token_type(st, _OpenBracket, IncompleteParsing))
      return;
    st->advance_token();

    int close_bracket_offset = find_bracketed_end(st, _CloseBracket, end_offset);
    if (!expect_condition(st, close_bracket_offset >= 0 && close_bracket_offset < end_offset, SyntaxError))
      return;
    index = parse_expression(st, close_bracket_offset);
    if (!expect_token_type(st, _CloseBracket, IncompleteParsing))
      return;
    st->advance_token();

  // it's an unpackingformat
  } else {
    array = shared_ptr<Expression>();
    index = shared_ptr<Expression>();
    format = parse_unpacking_format(st, end_offset);
  }
}

#endif

static shared_ptr<SimpleStatement> parse_simple_statement(ParserState* st, int end_offset) {

  static const TokenType augment_operator_tokens[] = {
    _PlusEquals, _MinusEquals, _AsteriskEquals, _SlashEquals, _PercentEquals,
    _AndEquals, _OrEquals, _XorEquals, _LeftShiftEquals, _RightShiftEquals,
    _DoubleTimesEquals, _DoubleSlashEquals};

  int operator_type, operator_offset = find_bracketed_end(st, _Equals, end_offset);
  for (operator_type = 0; operator_type < _AugmentOperatorCount && operator_offset == -1; operator_type++)
    operator_offset = find_bracketed_end(st, augment_operator_tokens[operator_type], end_offset);
  operator_type--;

  // now operator_offset is the offset of the operator
  // and operator_type is the AugmentOperator enum value, or -1 for an equals

  // if there's no operator, then it's just a general expression - maybe a function call
  if (operator_offset < 0)
    return shared_ptr<ExpressionStatement>(new ExpressionStatement(parse_expression(st, end_offset)));

  if (operator_type == -1) {

    shared_ptr<AssignmentStatement> stmt(new AssignmentStatement());
    parse_expression_list(st, stmt->left, operator_offset);

    for (int x = 0; x < stmt->left.size(); x++)
      if (!expect_condition(st, stmt->left[x]->valid_lvalue(), InvalidAssignment))
        return shared_ptr<SimpleStatement>();
    if (!expect_token_type(st, _Equals, IncompleteParsing))
      return shared_ptr<SimpleStatement>();
    st->advance_token();

    parse_expression_list(st, stmt->right, end_offset);
    return stmt;

  } else {

    shared_ptr<AugmentStatement> stmt(new AugmentStatement());
    stmt->oper = (AugmentOperator)operator_type;
    parse_expression_list(st, stmt->left, operator_offset);

    for (int x = 0; x < stmt->left.size(); x++)
      if (!expect_condition(st, stmt->left[x]->valid_lvalue(), InvalidAssignment))
        return shared_ptr<SimpleStatement>();
    if (!expect_token_type(st, augment_operator_tokens[operator_type], IncompleteParsing))
      return shared_ptr<SimpleStatement>();
    st->advance_token();

    parse_expression_list(st, stmt->right, end_offset);
    return stmt;

  }

#if 0

  shared_ptr<Expression> array, index;
  shared_ptr<UnpackingFormat> format;
  parse_equals_left(st, operator_offset, array, index, format);

  // there are some special cases for the equals-statement
  if (operator_type == -1) {
    if (!expect_token_type(st, _Equals, IncompleteParsing))
      return shared_ptr<SimpleStatement>();
    st->advance_token();

    // if it's a setitem-statement, just parse the right and we're done
    if (array && index && !format) {
      shared_ptr<SetitemAssignmentStatement> stmt(new SetitemAssignmentStatement());
      stmt->array = array;
      stmt->index = index;
      parse_expression_list(st, stmt->right, end_offset);
      return stmt;

    // if it's a normal statement, expect some more equalses maybe
    } else if (!array && !index && format) {
      shared_ptr<SimpleAssignmentStatement> stmt(new SimpleAssignmentStatement());
      stmt->left.push_back(format);
      while ((operator_offset = find_bracketed_end(st, _Equals, end_offset)) >= 0) {
        parse_equals_left(st, operator_offset, array, index, format);
        if (!expect_condition(st, !array && !index && format, SyntaxError))
          return shared_ptr<SimpleStatement>();
        stmt->left.push_back(format);
        if (!expect_token_type(st, _Equals, IncompleteParsing))
          return shared_ptr<SimpleStatement>();
        st->advance_token();
      }
      parse_expression_list(st, stmt->right, end_offset);
      return stmt;
    }

  } else {

    if (!expect_token_type(st, augment_operator_tokens[operator_type], IncompleteParsing))
      return shared_ptr<SimpleStatement>();
    st->advance_token();

    // if it's a setitem-statement, just parse the right and we're done
    if (array && index && !format) {
      shared_ptr<SetitemAugmentStatement> stmt(new SetitemAugmentStatement());
      stmt->oper = (AugmentOperator)operator_type;
      stmt->array = array;
      stmt->index = index;
      parse_expression_list(st, stmt->right, end_offset);
      return stmt;

    // if it's a normal statement, oh hey we're also done in this case
    } else if (!array && !index && format) {
      shared_ptr<SimpleAugmentStatement> stmt(new SimpleAugmentStatement());
      stmt->oper = (AugmentOperator)operator_type;
      stmt->left = format;
      parse_expression_list(st, stmt->right, end_offset);
      return stmt;
    }
  }

#endif

  st->set_parse_error(IncompleteParsing, "no simple-statement parsing rules matched");
  return shared_ptr<SimpleStatement>();
}

static void parse_compound_statement_suite(ParserState* st, shared_ptr<CompoundStatement> compound, int end_offset) {

  LocalParserState local;

  while ((st->token_num < end_offset) && (st->error() == NoParseError)) {

    bool newline_expected = true;
    bool should_clear_local = true;
    int line_end_offset = find_bracketed_end(st, _Newline, end_offset);
    if (line_end_offset < 0)
      line_end_offset = end_offset;

    switch (st->head_token().type) {

      case _Comment: // skip it
        st->advance_token();
      case _Newline: // skip this too
        should_clear_local = false;
        break;

      case _StringConstant: // probably a docstring. no normal statement starts with a constant
        if (st->token_at(st->token_num + 1).type == _Newline) {
          st->advance_token();
          break;
        }
      case _Dynamic: // expect a generic statement
      case _OpenParen:
      	compound->suite.push_back(parse_simple_statement(st, line_end_offset));
        break;

      case Print: { // PrintStatement
        st->advance_token();
        shared_ptr<PrintStatement> print(new PrintStatement);

        // check if there's a stream
        int comma_offset;
        if (st->head_token().type == _RightShift) {
          st->advance_token();
          comma_offset = find_bracketed_end(st, _Comma, end_offset);
          if (!expect_condition(st, (comma_offset >= 0) && (comma_offset <= line_end_offset), SyntaxError))
            break;
          print->stream = parse_expression(st, comma_offset);
          if (!expect_offset(st, comma_offset, IncompleteParsing))
            break;
          st->advance_token();
        }

        // if it ends with a comma, suppress the comma and set suppress_newline
        if (st->stream->tokens[line_end_offset - 1].type == _Comma) {
          print->suppress_newline = true;
          line_end_offset--;
        }

        // parse each comma-delimited expression
        parse_expression_list(st, print->items, line_end_offset);

        // if we suppress the newline, expect a comma
        if (print->suppress_newline && expect_token_type(st, _Comma, IncompleteParsing))
          st->advance_token();

        compound->suite.push_back(print);
        break; }
      case Del: { // DelStatement
        st->advance_token();
        shared_ptr<DeleteStatement> del(new DeleteStatement);
        parse_expression_list(st, del->items, line_end_offset);
        compound->suite.push_back(del);
        break; }



      case Pass: // PassStatement
        st->advance_token();
      	compound->suite.push_back(shared_ptr<PassStatement>(new PassStatement()));
        break;

      case Break: // BreakStatement
        st->advance_token();
      	compound->suite.push_back(shared_ptr<BreakStatement>(new BreakStatement()));
        break;

      case Continue: // ContinueStatement
        st->advance_token();
        compound->suite.push_back(shared_ptr<ContinueStatement>(new ContinueStatement()));
        break;

      case Return: { // ReturnStatement; expect an optional expression
        shared_ptr<ReturnStatement> ret(new ReturnStatement());
        st->advance_token();
        if (st->head_token().type != _Newline)
          parse_expression_list(st, ret->items, line_end_offset);
        compound->suite.push_back(ret);

        break; }

      case Raise: { // RaiseStatement; expect an optional expression
        shared_ptr<RaiseStatement> raise(new RaiseStatement());
        st->advance_token();
        if (st->head_token().type != _Newline) {
          vector<shared_ptr<Expression> > exprs;
          parse_expression_list(st, exprs, line_end_offset);
          if (exprs.size() > 0)
            raise->type = exprs[0];
          if (exprs.size() > 1)
            raise->value = exprs[1];
          if (exprs.size() > 2)
            raise->traceback = exprs[2];
          expect_condition(st, exprs.size() <= 3, TooManyArguments);
        }
        compound->suite.push_back(raise);
        break; }



      case Import: { // ImportStatement
        st->advance_token();
        shared_ptr<ImportStatement> imp(new ImportStatement());

        // read module names, then "as ..." if present
        imp->module_names = parse_dynamic_list(st);
        if (st->head_token().type == As) {
          st->advance_token();
          imp->module_renames = parse_dynamic_list(st);
          expect_condition(st, imp->module_names.size() == imp->module_renames.size(), UnbalancedImportStatement);
        }
        compound->suite.push_back(imp);
        break; }

      case From: { // ImportStatement
        st->advance_token();
        shared_ptr<ImportStatement> imp(new ImportStatement());

        // read the module name (there should be only one)
        if (!expect_token_type(st, _Dynamic, SyntaxError))
          break;
        imp->module_names.push_back(st->head_token().string_data);
        st->advance_token();

        // followed by "import"
        if (!expect_token_type(st, Import, SyntaxError))
          break;
        st->advance_token();

        // if it's a *, then set import_star
        if (st->head_token().type == _Asterisk) {
          imp->import_star = true;
          st->advance_token();

        // otherwise it's a list of symbols with an optional "as ..."
        } else {
          imp->symbol_list = parse_dynamic_list(st);
          if (st->head_token().type == As) {
            st->advance_token();
            imp->symbol_renames = parse_dynamic_list(st);
            expect_condition(st, imp->symbol_list.size() == imp->symbol_renames.size(), UnbalancedImportStatement);
          }
        }
        compound->suite.push_back(imp);
        break; }



      case Def: { // FunctionDefinition
        st->advance_token();
        shared_ptr<FunctionDefinition> fd(new FunctionDefinition());

        // read the name
        if (!expect_token_type(st, _Dynamic, SyntaxError))
          break;
        fd->name = st->head_token().string_data;
        st->advance_token();

        // open paren...
        if (!expect_token_type(st, _OpenParen, SyntaxError))
          break;
        st->advance_token();

        // parse the args
        int args_end_offset = find_bracketed_end(st, _CloseParen, end_offset);
        if (!expect_condition(st, args_end_offset >= 0, BracketingError))
          break;
        parse_function_argument_definition(st, fd->args, args_end_offset);
        if (!expect_offset(st, args_end_offset, IncompleteParsing))
          break;

        // close paren
        if (!expect_token_type(st, _CloseParen, SyntaxError))
          break;
        st->advance_token();

        parse_suite_from_colon(st, fd, end_offset);

        // eat up any decorators that might already exist
        fd->decorators = local.decorator_stack;

        compound->suite.push_back(fd);
        newline_expected = false;
        break; }

      case Global: { // GlobalStatement; expect a comma-separated list of _Dynamics
        st->advance_token();
        shared_ptr<GlobalStatement> stmt(new GlobalStatement());
        stmt->names = parse_dynamic_list(st);
        compound->suite.push_back(stmt);
        break; }

      case Exec: { // ExecStatement; expect an expression, 2x optional comma and another expression
        shared_ptr<ExecStatement> exec(new ExecStatement());
        st->advance_token();
        vector<shared_ptr<Expression> > exprs;
        parse_expression_list(st, exprs, line_end_offset);
        if (exprs.size() > 0)
          exec->code = exprs[0];
        if (exprs.size() > 1)
          exec->globals = exprs[1];
        if (exprs.size() > 2)
          exec->locals = exprs[2];
        expect_condition(st, exprs.size() <= 3, TooManyArguments);
        compound->suite.push_back(exec);
        break; }

      case Assert: { // AssertStatement; expect an expression, optional comma and another expression
        shared_ptr<AssertStatement> assert(new AssertStatement());
        st->advance_token();
        if (st->head_token().type != _Newline) {
          vector<shared_ptr<Expression> > exprs;
          parse_expression_list(st, exprs, line_end_offset);
          if (exprs.size() > 0)
            assert->check = exprs[0];
          if (exprs.size() > 1)
            assert->failure_message = exprs[1];
          expect_condition(st, exprs.size() <= 2, TooManyArguments);
        }
        compound->suite.push_back(assert);
        break; }

      case If: { // IfStatement; expect an expression, colon, [(newline, indent) or (statement)]
        local.clear();
        st->advance_token();
        shared_ptr<IfStatement> i(new IfStatement());

        // parse the expression
        int colon_offset = find_bracketed_end(st, _Colon, end_offset);
        if (!expect_condition(st, colon_offset >= 0, SyntaxError))
          break;
        i->check = parse_expression(st, colon_offset);
        if (!expect_offset(st, colon_offset, IncompleteParsing))
          break;

        parse_suite_from_colon(st, i, end_offset);
        compound->suite.push_back(i);
        local.prev_if = i;
        newline_expected = false;
        should_clear_local = false;
        break; }

      case Else: { // ElseStatement; expect a colon, [(newline, indent) or (statement)]
        if (!local.expect_else(st))
          break;

        st->advance_token();
        if (!expect_token_type(st, _Colon, SyntaxError))
          break;

        shared_ptr<ElseStatement> i(new ElseStatement());
        parse_suite_from_colon(st, i, end_offset);

        if (local.prev_if)
          local.prev_if->else_suite = i;
        else if (local.prev_for)
          local.prev_for->else_suite = i;
        else if (local.prev_while)
          local.prev_while->else_suite = i;
        else if (local.prev_try) {
          local.prev_try->else_suite = i;
          should_clear_local = false;
        }

        newline_expected = false;
        break; }

      case Elif: { // ElifStatement; expect an expression, colon, [(newline, indent) or (statement)]
        if (!local.expect(st, false, true, false, false, false)) // if-statement only
          break;

        st->advance_token();
        shared_ptr<SingleIfStatement> i(new ElifStatement());

        // parse the expression
        int colon_offset = find_bracketed_end(st, _Colon, end_offset);
        if (!expect_condition(st, colon_offset >= 0, SyntaxError))
          break;
        i->check = parse_expression(st, colon_offset);
        if (!expect_offset(st, colon_offset, IncompleteParsing))
          break;

        parse_suite_from_colon(st, i, end_offset);
        local.prev_if->elifs.push_back(i);
        newline_expected = false;
        should_clear_local = false;
        break; }

      case While: { // WhileStatement; expect an expression, colon, [(newline, indent) or (statement)]
        local.clear();
        st->advance_token();
        shared_ptr<WhileStatement> w(new WhileStatement());

        // parse the expression
        int colon_offset = find_bracketed_end(st, _Colon, end_offset);
        if (!expect_condition(st, colon_offset >= 0, SyntaxError))
          break;
        w->condition = parse_expression(st, colon_offset);
        if (!expect_offset(st, colon_offset, IncompleteParsing))
          break;

        parse_suite_from_colon(st, w, end_offset);
        compound->suite.push_back(w);
        local.prev_while = w;
        newline_expected = false;
        should_clear_local = false;
        break; }

      case For: { // ForStatement; expect a _Dynamic list, In, expression, colon, [(newline, indent) or (statement)]
        local.clear();
        st->advance_token();
        shared_ptr<ForStatement> fr(new ForStatement());

        // parse the unpacking information
        int in_offset = find_bracketed_end(st, In, end_offset);
        if (!expect_condition(st, in_offset > 0, SyntaxError))
          break;
        fr->variables = parse_unpacking_format(st, in_offset);

        // now the 'in'
        if (!expect_token_type(st, In, SyntaxError))
          break;
        st->advance_token();

        // parse the expressions
        int colon_offset = find_bracketed_end(st, _Colon, end_offset);
        if (!expect_condition(st, colon_offset >= 0, SyntaxError))
          break;
        parse_expression_list(st, fr->in_exprs, colon_offset);
        if (!expect_offset(st, colon_offset, IncompleteParsing))
          break;

        parse_suite_from_colon(st, fr, end_offset);
        compound->suite.push_back(fr);
        local.prev_for = fr;
        newline_expected = false;
        should_clear_local = false;
        break; }

      case Try: {  // TryStatement; expect a colon, [(newline, indent) or (statement)]
        local.clear();
        st->advance_token();
        if (!expect_token_type(st, _Colon, SyntaxError))
          break;

        shared_ptr<TryStatement> t(new TryStatement());
        parse_suite_from_colon(st, t, end_offset);
        compound->suite.push_back(t);
        local.prev_try = t;

        newline_expected = false;
        should_clear_local = false;
        break; }

      case Except: { // ExceptStatement
        if (!local.expect(st, false, false, false, false, true)) // try-statement only
          break;

        st->advance_token();
        shared_ptr<ExceptStatement> e(new ExceptStatement());

        int colon_offset = find_bracketed_end(st, _Colon, end_offset);
        if (!expect_condition(st, colon_offset >= 0, SyntaxError))
          break;

        // check if there's an As or _Comma before the _Colon
        static const TokenType token_types[] = {_Comma, As};
        int as_offset, token_index;
        find_bracketed_any(st, token_types, 2, colon_offset, &as_offset, &token_index);
        if (as_offset < 0 || as_offset > colon_offset)
          as_offset = colon_offset;

        if (as_offset != st->token_num) {
          e->types = parse_expression(st, as_offset);
          if (!expect_offset(st, as_offset, IncompleteParsing))
            break;
          if (as_offset != colon_offset)
            st->advance_token();
        }
        if (colon_offset != st->token_num) {
          if (!expect_token_type(st, _Dynamic, SyntaxError))
            break;
          e->name = st->head_token().string_data;
          st->advance_token();
          if (!expect_offset(st, colon_offset, SyntaxError))
            break;
        }

        parse_suite_from_colon(st, e, end_offset);
        local.prev_try->excepts.push_back(e);
        newline_expected = false;
        should_clear_local = false;
        break; }

      case Finally: { // FinallyStatement; expect a colon, [(newline, indent) or (statement)]
        if (!local.expect(st, false, false, false, false, true)) // try-statement only
          break;

        st->advance_token();
        if (!expect_token_type(st, _Colon, SyntaxError))
          break;

        shared_ptr<FinallyStatement> f(new FinallyStatement());
        parse_suite_from_colon(st, f, end_offset);

        local.prev_try->finally_suite = f; // TODO: Make sure none of these are already set when we set them
        newline_expected = false;
        break; }

      case Class: { // ClassStatement
        st->advance_token();
        if (!expect_token_type(st, _Dynamic, SyntaxError))
          break;

        shared_ptr<ClassDefinition> cls(new ClassDefinition());
        cls->class_name = st->head_token().string_data;
        st->advance_token();

        if (st->head_token().type == _OpenParen) {
          st->advance_token();
          int close_paren_offset = find_bracketed_end(st, _CloseParen, line_end_offset);
          if (!expect_condition(st, close_paren_offset >= 0 && close_paren_offset < line_end_offset, SyntaxError))
            break;

          parse_expression_list(st, cls->parent_types, close_paren_offset);
          if (!expect_offset(st, close_paren_offset, IncompleteParsing))
            break;
          st->advance_token();
        }

        // eat up any decorators that might already exist
        cls->decorators = local.decorator_stack;

        parse_suite_from_colon(st, cls, end_offset);
        compound->suite.push_back(cls);
        newline_expected = false;
        break; }

      case With: { // WithStatement
        st->advance_token();
        shared_ptr<WithStatement> w(new WithStatement());

        int colon_offset = find_bracketed_end(st, _Colon, end_offset);
        if (!expect_condition(st, colon_offset >= 0, SyntaxError))
          break;

        while (st->head_token().type != _Colon && st->error() == NoParseError) {
          // check if there's a _Comma before the _Colon
          int comma_offset = find_bracketed_end(st, _Comma, colon_offset);
          if (comma_offset < 0 || comma_offset > colon_offset)
            comma_offset = colon_offset;

          int as_offset = find_bracketed_end(st, As, comma_offset);
          if (as_offset < 0 || as_offset > comma_offset)
            as_offset = comma_offset;

          w->items.push_back(parse_expression(st, as_offset));
          if (!expect_offset(st, as_offset, IncompleteParsing))
            break;

          if (as_offset != comma_offset) {
            st->advance_token();
            if (!expect_token_type(st, _Dynamic, IncompleteParsing))
              break;
            w->names.push_back(st->head_token().string_data);
            st->advance_token();

          } else
            w->names.push_back("");

          if (!expect_offset(st, comma_offset, IncompleteParsing))
            break;

          if (comma_offset != colon_offset) {
            if (!expect_token_type(st, _Comma, SyntaxError))
              break;
            st->advance_token();
          }
        }

        parse_suite_from_colon(st, w, end_offset);
        compound->suite.push_back(w);
        newline_expected = false;
        should_clear_local = false;
        break; }

      case Yield: { // YieldStatement
        shared_ptr<YieldStatement> yield(new YieldStatement());
        st->advance_token();
        if (st->head_token().type != _Newline)
          yield->expr = parse_expression(st, line_end_offset);
        compound->suite.push_back(yield);
        break; }

      case _At: // Decorator
        st->advance_token();
        local.decorator_stack.push_back(parse_expression(st, line_end_offset));
        should_clear_local = false;
        break;

      case _Indent: // this should have been handled in another case
      case _Unindent:
        st->set_parse_error(InvalidIndentationChange, "indent encountered out of line");
        break;
      default:
        st->set_parse_error(InvalidStartingTokenType, "line starts with an invalid token type");
    }

    // here, we expect to be at either EOF or a newline token
    if (newline_expected && expect_token_type(st, _Newline, ExtraDataAfterLine))
      st->advance_token();
    if (should_clear_local)
      local.clear();
  }

  expect_condition(st, local.decorator_stack.size() == 0, SyntaxError);
}

void parse_token_stream(const TokenStream* stream, PythonAST* ast) {
  ParserState st;
  st.stream = stream;
  st.token_num = 0;
  st.ast = ast;
  st.ast->root = shared_ptr<ModuleStatement>(new ModuleStatement());
  parse_compound_statement_suite(&st, st.ast->root, st.stream->tokens.size());
}

static const char* error_names[] = {
  "NoParseError",
  "UnimplementedFeature",
  "InvalidIndentationChange",
  "InvalidStartingTokenType",
  "ExtraDataAfterLine",
  "UnbalancedImportStatement",
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

const char* name_for_parse_error(ParseError type) {
  int t = type;
  if (t < 0 || t > sizeof(error_names) / sizeof(error_names[0]))
    return NULL;
  return error_names[t];
}