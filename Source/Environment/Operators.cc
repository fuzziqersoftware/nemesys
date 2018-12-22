#include "Operators.hh"

#include <vector>

using namespace std;


BinaryOperator binary_operator_for_augment_operator(AugmentOperator oper) {
  static const vector<BinaryOperator> m({
    BinaryOperator::Addition,
    BinaryOperator::Subtraction,
    BinaryOperator::Multiplication,
    BinaryOperator::Division,
    BinaryOperator::Modulus,
    BinaryOperator::And,
    BinaryOperator::Or,
    BinaryOperator::Xor,
    BinaryOperator::LeftShift,
    BinaryOperator::RightShift,
    BinaryOperator::Exponentiation,
    BinaryOperator::IntegerDivision,
  });
  return m.at(static_cast<size_t>(oper));
}
