# this if block should be ignored entirely by the analyzer and compiler. we test
# this by deliberately writing code that fails static analysis and compilation
# within the block
if 2 + 2 == 5:
  print("you should never see this message")
  print(5)  # compilation fails here; print() has no fragment that takes an Int
  x = 7
  x = 'omg'  # analysis fails here; the variable type changes

# there should be no condition check in the generated assembly for this block.
# we test this by deliberately writing code that doesn't compile (as above) but
# will never execute anyway
if (2 + 2 == 4) or print(5):
  print("you should always see this message")
