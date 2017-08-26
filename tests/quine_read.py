import posix
import sys

# read the contents of this file
fd = posix.open(__file__, posix.O_RDONLY)
data = posix.read(fd, 1024)
posix.close(fd)

# write the contents to stdout
posix.write(1, data)
