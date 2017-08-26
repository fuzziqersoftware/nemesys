import __nemesys__
import posix

posix.write(1, __nemesys__.module_source(__name__))
