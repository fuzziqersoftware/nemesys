import sys

def is_prime(a):
  i = 2
  limit = a // 2
  while i <= limit:
    if a % i == 0:
      return False
    i = i + 1
  return True

limit = int(sys.argv[1])
a = 2
while a < limit:
  if is_prime(a):
    print('%d is prime' % a)
  else:
    print('%d is not prime' % a)
  a = a + 1

print('done')
