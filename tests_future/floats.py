a = 1.5
b = 2.0
print("a + b should be 3.5: " + repr(a + b))
print("a - b should be -0.5: " + repr(a - b))
print("a * b should be 3: " + repr(a * b))
print("a * a should be 2.25: " + repr(a * a))
print("a / b should be 0.75: " + repr(a / b))
print("int(a + b) should be 3: " + repr(int(a + b)))
print("a // b should be 0: " + repr(a // b))

# test binary operations with different types
print("Int + Int = Int: " + repr(7 + 5))
print("Int + Float = Float: " + repr(7 + 5.0))
print("Float + Int = Float: " + repr(7.0 + 5))
print("Float + Float = Float: " + repr(7.0 + 5.0))

print("Int - Int = Int: " + repr(7 - 5))
print("Int - Float = Float: " + repr(7 - 5.0))
print("Float - Int = Float: " + repr(7.0 - 5))
print("Float - Float = Float: " + repr(7.0 - 5.0))

print("Int * Int = Int: " + repr(7 * 5))
print("Int * Float = Float: " + repr(7 * 5.0))
print("Float * Int = Float: " + repr(7.0 * 5))
print("Float * Float = Float: " + repr(7.0 * 5.0))

print("Int / Int = Float: " + repr(7 / 5))
print("Int / Float = Float: " + repr(7 / 5.0))
print("Float / Int = Float: " + repr(7.0 / 5))
print("Float / Float = Float: " + repr(7.0 / 5.0))

print("Int // Int = Int: " + repr(7 // 5))
print("Int // Float = Int: " + repr(7 // 5.0))
print("Float // Int = Int: " + repr(7.0 // 5))
print("Float // Float = Int: " + repr(7.0 // 5.0))
