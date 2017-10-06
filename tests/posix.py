import posix
import sys

# read the contents of this file
fd = posix.open(__file__, posix.O_RDONLY)
data = posix.read(fd, 1024)
posix.close(fd)
print('the file\'s read size is ' + repr(len(data)))

# check that the file's stat shows the correct size
st = posix.stat(__file__)
print('the file\'s stat size is ' + repr(st.st_size))
