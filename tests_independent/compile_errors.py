def test_function_polymorphism():
  # this should work when y is a string, but not otherwise
  def f(x, y):
    import test_globals
    return test_globals.test_string if x else y

  print(f(True, 'lol'))
  print(f(False, 'lol'))

  try:
    print(f(True, 3))
  except NemesysCompilerError as e:
    print('compiler error at %s:%d - %s' % (e.filename, e.line, e.message))
  else:
    assert False

test_function_polymorphism()


# TODO: there's a bug here... if -XNoEagerCompilation is given and these return
# type annotations are removed, this test fails with the error "cannot infer
# scope return type". this is because we only recompile the immediate caller
# scope when a JIT recompile happens, so when add() is recompiled, call_add() is
# also recompiled with add()'s float return type, but test_type_annotations()
# doesn't know that call_add()'s return type is now known too. figure out a way
# to deal with this broken dependency chain
def add(x: int, y: float, z) -> float:
  return x + y + z

def call_add(x, y, z) -> float:
  return add(x, y, z)

def test_type_annotations():
  # this should only work when arg types match exactly. but z has no type
  # annotation so it can be anything
  print(call_add(6, 5.5, 3))
  print(call_add(6, 5.5, 3.3))
  print(call_add(6, 5.5, True))

  try:
    print(call_add(6.5, 5, 3))
  except NemesysCompilerError as e:
    print('compiler error at %s:%d - %s' % (e.filename, e.line, e.message))
  else:
    assert False

  try:
    print(call_add('not a number', None, None))
  except NemesysCompilerError as e:
    print('compiler error at %s:%d - %s' % (e.filename, e.line, e.message))
  else:
    assert False

  try:
    print(call_add('not a number', None, False))
  except NemesysCompilerError as e:
    print('compiler error at %s:%d - %s' % (e.filename, e.line, e.message))
  else:
    assert False

test_type_annotations()
