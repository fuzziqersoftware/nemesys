x = 0

def f(delta=0):
  global x
  x = x + delta
  print('x is now %d; delta is %d' % (x, delta))

f(1)
f(2)
f(3)
f(-7)
f(1)
