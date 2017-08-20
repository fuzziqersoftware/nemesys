import __nemesys__

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

# TODO: test break and continue

print('while test result: ' + repr(test_while()))
print('for test result: ' + repr(test_for()))
