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
    print('could not compile function call: ' + e.message)
  else:
    assert False

test_function_polymorphism()
