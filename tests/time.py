import time

start = time.time()
time.sleep(0.2)
diff = time.time() - start

if diff > 0.3:
  print('slept for %g seconds (more than 0.3 seconds!)' % diff)
elif diff < 0.2:
  print('slept for %g seconds (less than 0.2 seconds!)' % diff)
else:
  print('slept for 0.2-0.3 seconds')
