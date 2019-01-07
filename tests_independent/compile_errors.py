
# this should work when y is a string, but not otherwise
def f(x, y):
  return 'omg' if x else y

print(f(True, 'lol'))
print(f(False, 'lol'))

try:
  print(f(True, 3))

except NemesysCompilerError as e:
  print('failed')
  print(e.message)

else:
  assert False
