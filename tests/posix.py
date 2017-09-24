import __nemesys__
import posix
import sys

print('this script is ' + repr(len(__nemesys__.module_source(__name__))) + ' bytes long, according to __nemesys__')

st = posix.stat(__file__)
print('this script is ' + repr(st.st_size) + ' bytes long, according to stat')

for k in posix.environ:
  print('environment variable: ' + repr(k))

for filename in posix.listdir():
  print('file in current directory: ' + filename)

pid = posix.fork()
if pid:
  print('parent process ' + repr(posix.getpid()) + ' ran child ' + repr(pid))
else:
  print('child process ' + repr(posix.getpid()) +  ' started')
