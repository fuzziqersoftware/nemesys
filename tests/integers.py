a = 0x204
b = 0x404
print("a | b should be 1540: " + repr(a | b))
print("a & b should be 4: " + repr(a & b))
print("a ^ b should be 1536: " + repr(a ^ b))
print("a << 2 should be 2064: " + repr(a << 2))
print("a >> 2 should be 129: " + repr(a >> 2))
print("a + b should be 1544: " + repr(a + b))
print("a - b should be -512: " + repr(a - b))
print("a * b should be 530448: " + repr(a * b))
print("b // a should be 1: " + repr(b // a))
