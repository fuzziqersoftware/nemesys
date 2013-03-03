# TODO: multiple decorators on function and on class

# TODO: dotted imports
import sys
from os import path as os_path
from gc import *

def test1():
  pass

def test1(arg1=None, arg2=True, arg3=False):
  # lol, a comment
  print "arg1 is %s" % arg1 # lol, another comment
  print "arg2 is %s and arg3 is %s" % (arg2, arg3)

def test2(arg, arg2):
  for x in [item[0] for item in arg if item[1]]:
    yield x
  for x, y in {item[0]: item[1] for item in arg2 if item[2]}.iteritems():
    yield (x, y)

def test3(arg1, *args, kwarg1=False, **kwargs):
  """Prints args and kwargs to stdout and stderr."""
  print 'arg1, kwarg1 = %s, %s', arg1, kwarg1
  print 'args', args, kwargs
  print >> sys.stderr, "args:", args, kwargs

def test4(list1, dict1):
  '''Deletes things from lists & dicts given as arguments.'''
  if list1 is not None:
    del list1[0]
  assert dict1 is not None
  assert 'name' in dict1, "field 'name' not in %r" % (dict1,)
  del dict1['name']

@memo
def testmath(x):
  # wow, just look at all those semicolons
  x += 1 ; yield x
  x -= 1;yield x
  x *= x ;yield x
  x /= 2; yield x
  x %= 100 ; yield x ;
  x &= 0xF ; yield x;
  x |= 0x20
  yield x
  x ^= 4
  yield x
  x <<= 2
  yield x
  x >>= 1
  yield x
  x **= 2
  yield x
  x //= 10
  yield x

def testloops(num):
  for x in range(num):
    if 2 == x:
      continue
    elif x > 10:
      break
    else:
      print x
  else:
    raise AssertionError("hurr durr")
  return x * 2 if x < 5 else x * 3

global_var = 0
def test_keywords()
  global global_var
  exec 'global_var = random.randint(1000)'
  iterations = 0
  while global_var > 1:
    if global_var % 2:
      global_var /= 2
    else:
      global_var = global_var * 3 + 1
    iterations += 1
  else:
    print 'iterations: %d' % iterations

def test_try():
  # also tests nonuniform indentation
  try:
    i = random.randint(1, 7)
    if i == 1:
      raise NotImplementedError()
    elif i == 2:
      raise ValueError()
    elif i == 3:
      raise AttributeError()
    elif i == 4:
      raise AssertionError()
    elif i == 5:
      raise NameError()
    elif i == 6:
      raise ImportError()
  except NotImplementedError, e:
                      print '1', e
  except ValueError as e:
    print '2', e
  except (AttributeError, AssertionError), e:
      print '3', e
  except (NameError, ImportError) as e:
        print '4', e
  else:
          print 'no exception'
  finally:
            print 'finally'

def memo(func):
    cache = {}
    @wraps(func)
    def wrap(*args):
        if args not in cache:
            cache[args] = func(*args)
        return cache[args]
    return wrap

@memo
class with_class:
  def __init__(self, lol):
    self.lol = lol
  def __enter__(self):
    print '__enter__', self.lol
  def __exit__(self, t, v, tb):
    print '__exit__', self.lol, t, v, tb

def test_with():
  with with_class(31) as item:
    print item.lol
  with with_class(31) as item:
    assert False

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

def test_tuples():
  assert list((10, 20, 30)) == [10, 20, 30, 40, 50][:3]
  assert list((10, 20, 30)) == [10, 20, 30][:]
  assert list((10, 20, 30)) == [1, 10, 20, 30][1:4]
  assert list((10, 20, 30)) == [1, 10, 20, 30][-3:]
  assert {1: 2, 3: 4} == {3: 4, 1: 2}

# multiline \
comment \
with \
backslashes