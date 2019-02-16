def gcd(a, b):
  while b:
    t = b
    b = a % b
    a = t
  return a

def run_test(limit):
  print('computing GCD of all pairs of integers in [1, ' + repr(limit) + ']^2')

  x = limit
  while x > 0:
    y = limit
    while y > 0:
      r = gcd(x, y)
      print('gcd of ' + repr(x) + ' and ' + repr(y) + ' is ' + repr(r))
      y = y - 1
    x = x - 1

run_test(100)
print('done')
