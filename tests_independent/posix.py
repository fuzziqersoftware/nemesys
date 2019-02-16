import __nemesys__
import posix
import sys

st = posix.stat(__file__)
assert st.st_size == len(__nemesys__.module_source(__name__))
print('this script is ' + repr(st.st_size) + ' bytes long, according to both stat and __nemesys__')

for k in posix.environ:
  print('environment variable: ' + repr(k))

for filename in posix.listdir():
  print('file in current directory: ' + filename)

pid = posix.fork()
if pid:
  print('parent process ' + repr(posix.getpid()) + ' ran child ' + repr(pid))
else:
  print('child process ' + repr(posix.getpid()) +  ' started')
