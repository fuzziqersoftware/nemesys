#pragma once

enum class UnaryOperator {
  LogicalNot = 0, // not
  Not,            // ~
  Positive,       // +
  Negative,       // -
  Yield,          // yield
};

enum class BinaryOperator {
  LogicalOr = 0,   // or
  LogicalAnd,      // and
  LessThan,        // <
  GreaterThan,     // >
  Equality,        // ==
  GreaterOrEqual,  // >=
  LessOrEqual,     // <=
  NotEqual,        // !=
  In,              // in
  NotIn,           // not in
  Is,              // is
  IsNot,           // is not
  Or,              // |
  And,             // &
  Xor,             // ^
  LeftShift,       // <<
  RightShift,      // >>
  Addition,        // +
  Subtraction,     // -
  Multiplication,  // *
  Division,        // /
  Modulus,         // %
  IntegerDivision, // //
  Exponentiation,  // **
};

enum class TernaryOperator {
  IfElse = 0, // x if y else z
};

enum class AugmentOperator {
  Addition = 0,    // +=
  Subtraction,     // -=
  Multiplication,  // *=
  Division,        // /=
  Modulus,         // %=
  And,             // &=
  Or,              // |=
  Xor,             // ^=
  LeftShift,       // <<=
  RightShift,      // >>=
  Exponentiation,  // **=
  IntegerDivision, // //=
  _AugmentOperatorCount,
};

BinaryOperator binary_operator_for_augment_operator(AugmentOperator oper);
