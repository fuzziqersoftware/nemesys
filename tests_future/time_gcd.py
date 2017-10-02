def gcd(a, b):
  while b:
    t = b
    b = a % b
    a = t
  return a

def f():
  limit = 3000
  print('computing GCD of all pairs of integers in [1, ' + repr(limit) + ']^2')

  x = limit
  while x > 0:
    y = limit
    while y > 0:
      gcd(x, y)
      # a = x
      # b = y
      # while b:
      #   t = b
      #   b = a % b
      #   a = t
      y = y - 1
    x = x - 1

f()
print('done')
