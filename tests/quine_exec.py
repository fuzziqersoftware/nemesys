import __nemesys__
import posix

posix.execv('/bin/cat', ['/bin/cat', __file__])

errno = __nemesys__.errno()
print('execv failed with error code ' + repr(errno) + ', which means: ' +
    posix.strerror(errno))
