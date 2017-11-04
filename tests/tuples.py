# a is iterable because its values are all the same type
a = (1, 2, 3, 4)
for x in a:
  print(repr(x))
else:
  print('done')
print('a[-4] is ' + repr(a[-4]))
print('a[-3] is ' + repr(a[-3]))
print('a[-2] is ' + repr(a[-2]))
print('a[-1] is ' + repr(a[-1]))
print('a[0] is ' + repr(a[0]))
print('a[1] is ' + repr(a[1]))
print('a[2] is ' + repr(a[2]))
print('a[3] is ' + repr(a[3]))

# b is also iterable
b = (0.1, 0.2, 0.3, 0.4)
for y in b:
  print(repr(y))
else:
  print('done')
print('b[-4] is ' + repr(b[-4]))
print('b[-3] is ' + repr(b[-3]))
print('b[-2] is ' + repr(b[-2]))
print('b[-1] is ' + repr(b[-1]))
print('b[0] is ' + repr(b[0]))
print('b[1] is ' + repr(b[1]))
print('b[2] is ' + repr(b[2]))
print('b[3] is ' + repr(b[3]))

# c is not iterable because it contains disparate types
c = ('c', 3, 5.6, True)
print('c[-4] is ' + repr(c[-4]))
print('c[-3] is ' + repr(c[-3]))
print('c[-2] is ' + repr(c[-2]))
print('c[-1] is ' + repr(c[-1]))
print('c[0] is ' + repr(c[0]))
print('c[1] is ' + repr(c[1]))
print('c[2] is ' + repr(c[2]))
print('c[3] is ' + repr(c[3]))
