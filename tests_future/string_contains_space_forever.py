data: str = input(">>> ")
while data:
  if ' ' in data:
    print("string contains spaces (" + repr(len(data)) + " chars)")
  else:
    print("string does not contain spaces (" + repr(len(data)) + " chars)")
  data = input(">>> ")
