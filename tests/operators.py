import sys

# some useful stuff

def fail(x):
  assert False, "fail() was called!"
  return x



# UNARY OPERATORS

# logical not
print('not None == ' + repr(not None))
print('not False == ' + repr(not False))
print('not True == ' + repr(not True))
print('not 3 == ' + repr(not 3))
print('not -10 == ' + repr(not -10))
print('not 0 == ' + repr(not 0))
print('not 0.1 == ' + repr(not 0.1))
print('not 0.0 == ' + repr(not 0.0))
print('not -0.0 == ' + repr(not -0.0))
print('not -3.4 == ' + repr(not -3.4))
print('not b\'\' == ' + repr(not b''))
print('not b\'z\' == ' + repr(not b'z'))
print('not \'\' == ' + repr(not ''))
print('not \'z\' == ' + repr(not 'z'))
print('not [] == ' + repr(not []))
print('not [2, 3, 4] == ' + repr(not [2, 3, 4]))

# bitwise not
print('~0 == ' + repr(~0))
print('~0x3FF == ' + repr(~0x3FF))
print('~False == ' + repr(~False))
print('~True == ' + repr(~True))

# positive
print('+False == ' + repr(+False))
print('+True == ' + repr(+True))
print('+0 == ' + repr(+0))
print('+34 == ' + repr(+34))
print('+0.0 == ' + repr(+0.0))
print('+3.4 == ' + repr(+3.4))

# negative
print('-False == ' + repr(-False))
print('-True == ' + repr(-True))
print('-0 == ' + repr(-0))
print('-34 == ' + repr(-34))
print('-0.0 == ' + repr(-0.0))
print('-3.4 == ' + repr(-3.4))



# BINARY OPERATORS

# boolean combine operator short-circuiting
print('True or fail(True) == ' + repr(True or fail(True)))
print('4 or fail(4) == ' + repr(4 or fail(4)))
print('3.2 or fail(3.2) == ' + repr(3.2 or fail(3.2)))
print('b\'x\' or fail(b\'x\') == ' + repr(b'x' or fail(b'x')))
print('\'x\' or fail(\'x\') == ' + repr('x' or fail('x')))
print('False and fail(False) == ' + repr(False and fail(False)))
print('0 and fail(0) == ' + repr(0 and fail(0)))
print('0.0 and fail(0.0) == ' + repr(0.0 and fail(0.0)))
print('b\'\' and fail(b\'\') == ' + repr(b'' and fail(b'')))
print('\'\' and fail(\'\') == ' + repr('' and fail('')))

# boolean combine operator correctness
print('False or False == ' + repr(False or False))
print('True or False == ' + repr(True or False))
print('False or True == ' + repr(False or True))
print('True or True == ' + repr(True or True))
print('False and False == ' + repr(False and False))
print('True and False == ' + repr(True and False))
print('False and True == ' + repr(False and True))
print('True and True == ' + repr(True and True))
print('0 or 0 == ' + repr(0 or 0))
print('0 or 4 == ' + repr(0 or 4))
print('3 or 0 == ' + repr(3 or 0))
print('3 or 4 == ' + repr(3 or 4))
print('0 and 0 == ' + repr(0 and 0))
print('0 and 4 == ' + repr(0 and 4))
print('3 and 0 == ' + repr(3 and 0))
print('3 and 4 == ' + repr(3 and 4))
print('0.0 or 0.0 == ' + repr(0.0 or 0.0))
print('0.0 or 4.0 == ' + repr(0.0 or 4.0))
print('3.0 or 0.0 == ' + repr(3.0 or 0.0))
print('3.0 or 4.0 == ' + repr(3.0 or 4.0))
print('0.0 and 0.0 == ' + repr(0.0 and 0.0))
print('0.0 and 4.0 == ' + repr(0.0 and 4.0))
print('3.0 and 0.0 == ' + repr(3.0 and 0.0))
print('3.0 and 4.0 == ' + repr(3.0 and 4.0))
print('b\'\' or b\'\' == ' + repr(b'' or b''))
print('b\'\' or b\'x\' == ' + repr(b'' or b'x'))
print('b\'x\' or b\'\' == ' + repr(b'x' or b''))
print('b\'x\' or b\'x\' == ' + repr(b'x' or b'x'))
print('b\'\' and b\'\' == ' + repr(b'' and b''))
print('b\'\' and b\'x\' == ' + repr(b'' and b'x'))
print('b\'x\' and b\'\' == ' + repr(b'x' and b''))
print('b\'x\' and b\'x\' == ' + repr(b'x' and b'x'))
print('\'\' or \'\' == ' + repr('' or ''))
print('\'\' or \'x\' == ' + repr('' or 'x'))
print('\'x\' or \'\' == ' + repr('x' or ''))
print('\'x\' or \'x\' == ' + repr('x' or 'x'))
print('\'\' and \'\' == ' + repr('' and ''))
print('\'\' and \'x\' == ' + repr('' and 'x'))
print('\'x\' and \'\' == ' + repr('x' and ''))
print('\'x\' and \'x\' == ' + repr('x' and 'x'))

# ordered comparisons
print('False == True == ' + repr(False == True))
print('False != True == ' + repr(False != True))
print('False < True == ' + repr(False < True))
print('False > True == ' + repr(False > True))
print('False <= True == ' + repr(False <= True))
print('False >= True == ' + repr(False >= True))
print('-3 == 0 == ' + repr(-3 == 0))
print('-3 != 0 == ' + repr(-3 != 0))
print('-3 < 0 == ' + repr(-3 < 0))
print('-3 > 0 == ' + repr(-3 > 0))
print('-3 <= 0 == ' + repr(-3 <= 0))
print('-3 >= 0 == ' + repr(-3 >= 0))
print('0 == 0 == ' + repr(0 == 0))
print('0 != 0 == ' + repr(0 != 0))
print('0 < 0 == ' + repr(0 < 0))
print('0 > 0 == ' + repr(0 > 0))
print('0 <= 0 == ' + repr(0 <= 0))
print('0 >= 0 == ' + repr(0 >= 0))
print('2 == 20 == ' + repr(2 == 20))
print('2 != 20 == ' + repr(2 != 20))
print('2 < 20 == ' + repr(2 < 20))
print('2 > 20 == ' + repr(2 > 20))
print('2 <= 20 == ' + repr(2 <= 20))
print('2 >= 20 == ' + repr(2 >= 20))

# is/is not are tested in a separate script

# containment operators
print('b\'\' in b\'xyz\' == ' + repr(b'' in b'xyz'))
print('b\'x\' in b\'xyz\' == ' + repr(b'x' in b'xyz'))
print('b\'a\' in b\'xyz\' == ' + repr(b'a' in b'xyz'))
print('b\'\' not in b\'xyz\' == ' + repr(b'' not in b'xyz'))
print('b\'x\' not in b\'xyz\' == ' + repr(b'x' not in b'xyz'))
print('b\'a\' not in b\'xyz\' == ' + repr(b'a' not in b'xyz'))
print('\'\' in \'xyz\' == ' + repr('' in 'xyz'))
print('\'x\' in \'xyz\' == ' + repr('x' in 'xyz'))
print('\'a\' in \'xyz\' == ' + repr('a' in 'xyz'))
print('\'\' not in \'xyz\' == ' + repr('' not in 'xyz'))
print('\'x\' not in \'xyz\' == ' + repr('x' not in 'xyz'))
print('\'a\' not in \'xyz\' == ' + repr('a' not in 'xyz'))

# integer operators
print('39 || 5 == ' + repr(39 | 5))
print('39 && 5 == ' + repr(39 & 5))
print('39 ^ 5 == ' + repr(39 ^ 5))
print('39 << 5 == ' + repr(39 << 5))
print('39 >> 5 == ' + repr(39 >> 5))
print('39 + 5 == ' + repr(39 + 5))
print('39 - 5 == ' + repr(39 - 5))
print('39 * 5 == ' + repr(39 * 5))
print('39 / 5 == ' + repr(39 / 5))
print('39 // 5 == ' + repr(39 // 5))
print('39 % 5 == ' + repr(39 % 5))
print('39 ** 5 == ' + repr(39 ** 5))

# float operators
# TODO: we use a smaller exponent for ** because nemesys' repr(Float) isn't as
# precise as python3's repr(Float) yet
print('39.0 + 5 == ' + repr(39.0 + 5))
print('39.0 - 5 == ' + repr(39.0 - 5))
print('39.0 * 5 == ' + repr(39.0 * 5))
print('39.0 / 5 == ' + repr(39.0 / 5))
print('39.0 // 5 == ' + repr(39.0 // 5))
print('39.0 % 5 == ' + repr(39.0 % 5))
print('39.0 ** 3 == ' + repr(39.0 ** 3))
print('39 + 5.0 == ' + repr(39 + 5.0))
print('39 - 5.0 == ' + repr(39 - 5.0))
print('39 * 5.0 == ' + repr(39 * 5.0))
print('39 / 5.0 == ' + repr(39 / 5.0))
print('39 // 5.0 == ' + repr(39 // 5.0))
print('39 % 5.0 == ' + repr(39 % 5.0))
print('39 ** 3.0 == ' + repr(39 ** 3.0))
print('39.0 + 5.0 == ' + repr(39.0 + 5.0))
print('39.0 - 5.0 == ' + repr(39.0 - 5.0))
print('39.0 * 5.0 == ' + repr(39.0 * 5.0))
print('39.0 / 5.0 == ' + repr(39.0 / 5.0))
print('39.0 // 5.0 == ' + repr(39.0 // 5.0))
print('39.0 % 5.0 == ' + repr(39.0 % 5.0))
print('39.0 ** 3.0 == ' + repr(39.0 ** 3.0))

# exponentiation has a special case for Int and Int when exponent < 0; nemesys
# doesn't implement this (it raises ValueError) because it can't dynamically
# change the variable type
if sys.version == 'nemesys':
  try:
    x = 39 ** -5
    print('negative exponentiation is incorrect')
  except ValueError:
    print('negative exponentiation is correct')
else:
  x = 39 ** -5
  if (x > 0) and (x < 1):
    print('negative exponentiation is correct')
  else:
    print('negative exponentiation is incorrect')



# TERNARY OPERATOR

print('fail(2) if False else 3 == ' + repr(fail(2) if False else 3))
print('2 if True else fail(3) == ' + repr(2 if True else fail(3)))
print('fail(3) if False else 2 == ' + repr(fail(3) if False else 2))
print('3 if True else fail(2) == ' + repr(3 if True else fail(2)))



# uncomment for curiosity
#import __nemesys__
#print('this module compiles to ' + repr(__nemesys__.module_compiled_size(__name__)) + ' bytes')
