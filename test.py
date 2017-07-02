import test  # modules can import themselves
from test_compiler import y, x_from_test_compiler

x_from_test = 5  # this is imported in test_parser_lexer
y_from_test = 6  # this is imported in test_parser_lexer

def g(w):
  print('in g(), x_from_test_compiler is %r and y is %r and w is %r' % (
      x_from_test_compiler, y, w))
  return w

def f():
  z = 7
  print('in f(), x_from_test_compiler is %r and y is %r and z in %r' % (
      x_from_test_compiler, y, z))
  return z

w = g(f())
