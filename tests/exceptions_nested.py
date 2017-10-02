try:
  try:
    print('whatever you do, don\'t type a string with the letter y in it')
    value = input()
    assert 'y' not in value, 'you typed: ' + value
    print('code after the assert statement executed')

  except MemoryError as e2:
    print('something went horribly horribly wrong')

  else:
    print('the string did not contain y - all is well')

  finally:
    print('it is not yet the end of days')

except AssertionError as e1:
  print('nooooo you monster! what have you done!')
  #print(e1.message)  # works in nemesys, not in python

else:
  print('the string did not contain y - all is still well')

finally:
  print('it is now the end of days')
