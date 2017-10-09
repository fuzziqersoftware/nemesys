# these conditions should be short-circuited
if True:
  print('single if-statement short-circuited correctly')
else:
  nonexistent_function()  # this doesn't compile

if False:
  nonexistent_function()  # this doesn't compile
else:
  print('single if-statement short-circuited correctly')

x = int(input())
if x == 0:
  print('you entered 0')
elif x == 1:
  print('you entered 1')
elif x == 2:
  print('you entered 2')
else:
  print('you didn\'t enter 0, 1, or 2')
