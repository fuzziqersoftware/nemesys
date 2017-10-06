import posix

try:
  print(int('abc'))
except ValueError:
  print('can\'t convert \'abc\' to an int')

try:
  # this fd should never be open in this test
  posix.close(0x1000000)
  print('somehow managed to close ridiculous fd')
except OSError as e:
  print('can\'t close ridiculous fd; errno=' + repr(e.errno) + ' (' + posix.strerror(e.errno) + ')')
