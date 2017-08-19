import tests.functions  # modules can import themselves
from tests.globals import y, z

z = 10

def g(w):
  print('in g(), z is ' + repr(z) + ' and y is ' + repr(y) + ' and w is ' + repr(w))
  return w

def f():
  z = 7
  print('in f(), y is ' + repr(y) + ' and z is ' + repr(z))
  return z

w = g(f())

print('after all of that, w is ' + repr(w) + ' and z is ' + repr(z))

# this is supposed to print the following:
# in f(), y is 4 and z is 7
# in g(), z is -8 and y is 4 and w is 7
# after all of that, w is 7
