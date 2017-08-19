def test_while():
  z = 10
  while z > 0:
    print('z is ' + repr(z) + ' and is it > 0? ' + repr(z > 0))
    z = z - 3
  print('z is ' + repr(z) + ' and is it > 0? ' + repr(z > 0))
  return z

# TODO: test for loops
# TODO: test else clauses
# TODO: test break and continue

print('while test result: ' + repr(test_while()))
