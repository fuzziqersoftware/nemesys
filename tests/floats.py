a = 1.5
b = 2.0
print("a + b should be 3.5: %g" % (a + b))
print("a - b should be -0.5: %g" % (a - b))
print("a * b should be 3: %g" % (a * b))
print("a * a should be 2.25: %g" % (a * a))
print("a / b should be 0.75: %g" % (a / b))
print("int(a + b) should be 3: %d" % (int(a + b)))
print("a // b should be 0: %g" % (a // b))

# test binary operations with different types
print("Int + Int = Int: %d" % (7 + 5))
print("Int + Float = Float: %g" % (7 + 5.0))
print("Float + Int = Float: %g" % (7.0 + 5))
print("Float + Float = Float: %g" % (7.0 + 5.0))

print("Int - Int = Int: %d" % (7 - 5))
print("Int - Float = Float: %g" % (7 - 5.0))
print("Float - Int = Float: %g" % (7.0 - 5))
print("Float - Float = Float: %g" % (7.0 - 5.0))

print("Int * Int = Int: %d" % (7 * 5))
print("Int * Float = Float: %g" % (7 * 5.0))
print("Float * Int = Float: %g" % (7.0 * 5))
print("Float * Float = Float: %g" % (7.0 * 5.0))

print("Int / Int = Float: %g" % (7 / 5))
print("Int / Float = Float: %g" % (7 / 5.0))
print("Float / Int = Float: %g" % (7.0 / 5))
print("Float / Float = Float: %g" % (7.0 / 5.0))

print("Int // Int = Int: %d" % (7 // 5))
print("Int // Float = Float: %g" % (7 // 5.0))
print("Float // Int = Float: %g" % (7.0 // 5))
print("Float // Float = Float: %g" % (7.0 // 5.0))
