import posix

posix.execv('/bin/cat', ['/bin/cat', __file__])
