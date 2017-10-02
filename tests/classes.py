num_objects = 0

class BoringTestClass:
  def __init__(self, name='', value=0, print_destruction=True):
    global num_objects
    self.name = name
    self.value = value
    self.print_destruction = print_destruction
    num_objects = num_objects + 1
    print('creating BoringTestClass object with name=' + self.name + ', value=' + repr(self.value))

  def __del__(self):
    global num_objects
    num_objects = num_objects - 1
    if self.print_destruction:
      print('destroying BoringTestClass object with name=' + self.name + ', value=' + repr(self.value))

  def set_value(self, value=0):
    old_value = self.value
    self.value = value
    return old_value

  def print_value(self):
    print(self.name + ' == ' + repr(self.value))

def test():
  x = BoringTestClass('x', 30)
  x.print_value()
  x.set_value(50)
  x.print_value()

print('there are ' + repr(num_objects) + ' BoringTestClass objects')
test()
print('there are ' + repr(num_objects) + ' BoringTestClass objects')

# unlike python, nemesys doesn't destroy objects at the global scope on shutdown
y = BoringTestClass('y', 40, print_destruction=False)
y.print_value()
print('there are ' + repr(num_objects) + ' BoringTestClass objects')

# x should be destroyed, but y shouldn't be since it's a module global
