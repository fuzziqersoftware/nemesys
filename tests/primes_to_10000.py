import math
import sys
import time

def is_prime(a):
  i = 2
  limit = a // math.sqrt(a)
  while i <= limit:
    if a % i == 0:
      return False
    i = i + 1
  return True

def print_primes_until(limit):
  a = 2
  while a < limit:
    if is_prime(a):
      print('%d is prime' % a)
    else:
      print('%d is not prime' % a)
    a = a + 1

print_primes_until(10000)
print('done')
