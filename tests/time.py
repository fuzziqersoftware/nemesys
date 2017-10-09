import time

start = time.time()
time.sleep(0.2)
diff = time.time() - start

if diff > 0.3:
  print('slept for more than 0.3 seconds')
elif diff < 0.2:
  print('slept for less than 0.2 seconds')
else:
  print('slept for 0.2-0.3 seconds')
