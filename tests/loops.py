def test_while():
  z = 10
  while z > 0:
    print('z is ' + repr(z) + ' and is it > 0? ' + repr(z > 0))
    z = z - 3
  else:
    print('while loop done; z is ' + repr(z) + ' and is it > 0? ' + repr(z > 0))
  return z

def test_for():
  # TODO: test bytes, unicodes, tuples, sets, and dicts
  z = [1, 2, 3, 4]
  for x in z:
    print(repr(x))
  else:
    print('for loop done')
  return x

def test_while_break_continue():
  x = 0
  while x < 10:
    x = x + 1
    if x == 3:
      continue
    if x == 6:
      break
    print(repr(x))
  else:
    print('else block executed')

def test_for_break_continue():
  z = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
  for x in z:
    if x == 3:
      continue
    if x == 6:
      break
    print(repr(x))
  else:
    print('else block executed')

print('while test result: ' + repr(test_while()))
print('for test result: ' + repr(test_for()))
test_while_break_continue()
test_for_break_continue()
