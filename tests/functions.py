import test_globals
from test_globals import y, z, get_z

z = 10

# check if basic argument passing and global references within functions work
def g(v):
  print('in g(), z is ' + repr(z) + ' and y is ' + repr(y) + ' and v is ' + repr(v))
  return v

# check if local names override global names in the function scope
def f():
  z = 7
  print('in f(), y is ' + repr(y) + ' and z is ' + repr(z))
  return z

# check that types are correctly assigned to indeterminate variables
w = g(f())

# check that get_z has the globals from test_globals, not from this module
print('get_z returns %d' % get_z())
test_globals.z = z
print('get_z returns %d' % get_z())

# check that the global z is not modified by f
print('after all of that, w is ' + repr(w) + ' and z is ' + repr(z))

# check if stack-based argument passing works properly
def sum_all_arguments(a, b, c, d, e=0, f=0, g=0, h=0, i=0, j=0):
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
  return a + b + c + d + e + f + g + h + i + j

# this function should work with integers, floats, and strings
int_sum = sum_all_arguments(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)
print('the sum of 10 ints is ' + repr(int_sum))
int_sum = sum_all_arguments(1, 2, 3, 4)
print('the sum of 4 ints is ' + repr(int_sum))
int_sum = sum_all_arguments(1, 2, 3, 4, h=8, j=10)
print('the sum of 6 ints is ' + repr(int_sum))
str_sum = sum_all_arguments('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j')
print('the sum of 10 strs is ' + repr(str_sum))
float_sum = sum_all_arguments(1, 2.0, 3.5, 4.0)
print('the sum of 4 floats is ' + repr(float_sum))

double = lambda x: x + x
a = 0
while a < 4:
  print('double(' + repr(a) + ') = ' + repr(double(a)))
  a = a + 1
print('double(' + repr('omg') + ') = ' + repr(double('omg')))
