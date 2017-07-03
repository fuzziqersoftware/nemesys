import tests.functions  # modules can import themselves
from tests.globals import y, z

def g(w):
  print('in g(), z is %r and y is %r and w is %r' % (z, y, w))
  return w

def f():
  z = 7
  print('in f(), y is %r and z is %r' % (y, z))
  return z

w = g(f())

print('after all of that, w is %r' % w)

# this is supposed to print the following:
# in f(), y is 4 and z is 7
# in g(), z is -8 and y is 4 and w is 7
# after all of that, w is 7
