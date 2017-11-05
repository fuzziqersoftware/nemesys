#include "PythonOperators.hh"

#include <unordered_map>

using namespace std;


BinaryOperator binary_operator_for_augment_operator(AugmentOperator oper) {
  static const unordered_map<AugmentOperator, BinaryOperator> m({
    {AugmentOperator::Addition, BinaryOperator::Addition},
    {AugmentOperator::Subtraction, BinaryOperator::Subtraction},
    {AugmentOperator::Multiplication, BinaryOperator::Multiplication},
    {AugmentOperator::Division, BinaryOperator::Division},
    {AugmentOperator::Modulus, BinaryOperator::Modulus},
    {AugmentOperator::And, BinaryOperator::And},
    {AugmentOperator::Or, BinaryOperator::Or},
    {AugmentOperator::Xor, BinaryOperator::Xor},
    {AugmentOperator::LeftShift, BinaryOperator::LeftShift},
    {AugmentOperator::RightShift, BinaryOperator::RightShift},
    {AugmentOperator::Exponentiation, BinaryOperator::Exponentiation},
    {AugmentOperator::IntegerDivision, BinaryOperator::IntegerDivision},
  });
  return m.at(oper);
}
