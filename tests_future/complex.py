# hey look it's a testing script lol ### #

# TODO: dotted imports
import test as test_lol, test_compiler
from test import x_from_test as wtf, y_from_test
from test_compiler import *

a = -10

def test0():
  pass

def test1(arg1=None, arg2=True, arg3=False):
  # lol, a comment
  print("arg1 is %s; " % arg1, end=' ') # lol, another comment
  print("arg2 is %s and arg3 is %s" % (arg2, arg3), "ok that's all")

def test2(arg, arg2):
  d = {item[0]: item[1] for item in arg2 if item[2]}
  for x in [a for a, b in arg if b]:
    yield (x, y, y_from_test)
  for x in d:
    yield (x, d[y])

def test3(arg1, *args, kwarg1=False, **kwargs):
  """Prints args and kwargs to stdout and stderr."""
  print('arg1, kwarg1 = %s, %s' % (arg1, kwarg1))
  print('args', args, kwargs)
  print("args:", args, kwargs, stream=sys.stderr)

def test4(list1, dict1):
  '''Deletes things from lists & dicts given as arguments.'''
  if list1 is not None:
    pass  # del list1[0]
  assert dict1 is not None
  assert 'name' in dict1, "field 'name' not in %r" % (dict1,)
  # del dict1['name']

def test5():
  a.b.c.d.e = 15
  a.c['omg'] = 'lolz'

# TODO: uncomment when decorators and AugmentStatement are implemented
# @lol
# @memo
# def testmath(x):
#   # wow, just look at all those semicolons
#   x += 1 ; yield x
#   x -= 1;yield x
#   x *= x ;yield x
#   x /= 2; yield x
#   x %= 100 ; yield x ;
#   x &= 0xF ; yield x;
#   x |= 0x20
#   yield x
#   x ^= 4
#   yield x
#   x <<= 2
#   yield x
#   x >>= 1
#   yield x
#   x **= 2
#   yield x
#   x //= 10
#   yield x
#   return -2**-2

def testloops(num):
  for x in [1, 2, 3]:  # TODO: range(num):
    if 2 == x:
      continue
    elif x > 10:
      break
    else:
      print(x)
  else:
    raise AssertionError("hurr durr")
  return x * 2 if x < 5 else x * 3

global_var = 0
def test_keywords():
  global global_var
  # exec 'global_var = random.randint(1000)'
  iterations = 0
  while global_var > 1:
    if global_var % 2:
      global_var /= 2
    else:
      global_var = global_var * 3 + 1
    iterations = iterations + 1
  else:
    print('iterations: %d' % iterations)

while a < 0:
  a = a + 1
else:
  a = 1

for x in range(10):
  print(x)

# also tests nonuniform indentation
try:
  i = 4  # os.getpid() % 6
  if i == 0:
    raise NotImplementedError()
  elif i == 1:
    raise ValueError()
  elif i == 2:
    raise AttributeError()
  elif i == 3:
    raise AssertionError()
  elif i == 4:
    raise NameError()
  elif i == 5:
    raise ImportError()
except NotImplementedError, e:
                    print('1', e)
except ValueError as e:
  print('2', e)
except (AttributeError, AssertionError), e:
    print('3', e)
except (NameError, ImportError) as e:
      print('4', e)
except KeyError:
    raise
except:
    print(wtf)
else:
        print('no exception')
finally:
          print('finally')

# def memo(func):
#     cache = {}
#     @wraps(func)
#     def wrap(*args):
#         if args not in cache:
#             cache[args] = func(*args)
#         return cache[args]
#     return wrap

# @memo
class with_class:
  hax = 9
  def __init__(self, lol):
    self.lol = lol
  def __enter__(self):
    print('__enter__', self.lol)
  def __exit__(self, t, v, tb):
    print('__exit__', self.lol, t, v, tb)

class omg(object):
  pass

class lol(object, omg, with_class):
  x = 3

def test_with():
  with with_class(31) as item:
    print(item.lol)
  with with_class(31) as item:
    assert False
  with with_class(32), \
      with_class(94) as omghax, \
      lol(with_class):
    pass

test_lambda = lambda x, y: x + 3 * y['number']
test_lambda_one = lambda y: 3 * y['number']
test_lambda_none = lambda: 'lol'

point_in_rect = lambda x, y, x1, y1, x2, y2: ((x >= x1) and (x <= x2) and \
                    (y >= y1) and (y <= y2)) or (x is None) or (y is None)
this_is_none = lambda x: x is None
this_is_not_none_1 = lambda x: not x is None
this_is_not_none_2 = lambda x: x is not None

this_in = lambda x,y: x in y
this_not_in_1 = lambda x,y: not x in y
this_not_in_2 = lambda x,y: x not in y

not_equal = lambda x, y: x <> y # :(

lambda_with_defaults = lambda x, y=0: x + y * 4

# multiline \
comment \
with \
backslashes

long_list_definition = [
  1,
  2,
  3,
  4,
]

long_dict_definition = {
  1: 'a',
  2: lambda x: x * 2,
  3: 5,
  4: None,
}

long_set_definition = {
  13,
  None,
  "LOL",
  5.0,
}

empty_tuple = ()
empty_list = []
empty_dict = {}

def test_tuples():
  assert list((10, 20, 30)) == [10, 20, 30, 40, 50][:3]
  assert list((10, 20, 30)) == [10, 20, 30][:]
  assert list((10, 20, 30)) == [1, 10, 20, 30][1:4]
  assert list((10, 30)) == [1, 10, 20, 30][1::2]
  assert list((10,)) == [1, 10, 20, 30][1:2:2]
  assert list((1,)) == [1, 10, 20, 30][:2:2]
  assert list((10, 20, 30)) == [1, 10, 20, 30][-3:]
  assert {1: 2, 3: 4} == {3: 4, 1: 2}
  if False:
    pass

def test_strings():
  assert b'' == bytes()
  assert u'' == unicode()
  assert '' == u''
  x = (
    u""" \""" """,
    b''' \''' ''',
    ''' \''' ''',
    u" \"",
    b" \"",
    ' \r \n \t \x02 \' ')

if __name__ == '__main__':
  print(test_tuples)
  print(test_tuples())
