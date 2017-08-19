import tests.functions  # modules can import themselves
from tests.globals import y, z

z: Int = 10

# this checks if argument passing and global references within functions work
def g(v):
  print('in g(), z is ' + repr(z) + ' and y is ' + repr(y) + ' and v is ' + repr(v))
  return v

# this checks if local names override global names in the function scope
def f():
  z = 7
  print('in f(), y is ' + repr(y) + ' and z is ' + repr(z))
  return z

# this checks that types are correctly assigned to indeterminate variables
w = g(f())

# this checks that the global z is not modified by f
print('after all of that, w is ' + repr(w) + ' and z is ' + repr(z))

# this checks if stack-based argument passing works properly
def sum_all_arguments(a, b, c, d, e, f, g, h, i, j):
  print('a is ' + repr(a))
  print('b is ' + repr(b))
  print('c is ' + repr(c))
  print('d is ' + repr(d))
  print('e is ' + repr(e))
  print('f is ' + repr(f))
  print('g is ' + repr(g))
  print('h is ' + repr(h))
  print('i is ' + repr(i))
  print('j is ' + repr(j))
  print('the sum of 10 arguments is ' + repr(a + b + c + d + e + f + g + h + i + j))

sum_all_arguments(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)
