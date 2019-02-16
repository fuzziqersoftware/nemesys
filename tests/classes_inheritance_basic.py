# this part tests inheriting data attributes from parent classes

class BoringTestClass1:
  def __init__(self, v1=0):
    self.v1 = v1


class BoringTestClass2(BoringTestClass1):
  def __init__(self, v2=0):
    BoringTestClass1.__init__(self, v2 * 2)
    self.v2 = v2


class BoringTestClass3(BoringTestClass2):
  def __init__(self, v3=0):
    BoringTestClass2.__init__(self, v3 * 2)
    self.v3 = v3


def test1():
  x = BoringTestClass3(3)
  print(repr(x.v1))
  print(repr(x.v2))
  print(repr(x.v3))

test1()



# this part tests inheriting data attributes and functions, and calling super
# functions that aren't __init__

class BoringTestParentClass:
  def __init__(self, v1=0, v2=0):
    self.v1 = v1
    self.v2 = v2

  def set_v1(self, v1=0):
    self.v1 = v1

  def set_v2(self, v2=0):
    self.v2 = v2

  def print_values(self):
    print("v1 == " + repr(self.v1))
    print("v2 == " + repr(self.v2))


class BoringTestChildClass(BoringTestParentClass):
  def __init__(self, v3=0):
    BoringTestParentClass.__init__(self, v3, v3)  # sets v1 and v2
    self.v3 = v3

  def set_v1(self, v1=0):  # override; actually sets it to v1**2
    self.v1 = v1 * v1

  # set_v2 is inherited from the parent class

  def set_v3(self, v3=0):
    self.v3 = v3

  def print_values(self):
    BoringTestParentClass.print_values(self)
    print("v3 == " + repr(self.v3))


def test2():
  x = BoringTestChildClass(3)
  x.print_values()
  x.set_v1(4)
  x.print_values()
  x.set_v2(5)
  x.print_values()
  x.set_v3(6)
  x.print_values()

test2()
