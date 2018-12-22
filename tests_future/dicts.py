d1 = {1: 2, 3: 4, 5: 6}
d2 = {1: '2', 3: '4', 5: '6'}
d3 = {'1': 2, '3': 4, '5': 6}
d4 = {'1': '2', '3': '4', '5': '6'}


# in / not in

print('1 in d1 == %d' % (1 in d1,))
print('1 in d2 == %d' % (1 in d2,))
print('1 in d3 == %d' % (1 in d3,))
print('1 in d4 == %d' % (1 in d4,))
print('\'1\' in d1 == %d' % ('1' in d1,))
print('\'1\' in d2 == %d' % ('1' in d2,))
print('\'1\' in d3 == %d' % ('1' in d3,))
print('\'1\' in d4 == %d' % ('1' in d4,))

print('1 not in d1 == %d' % (1 not in d1,))
print('1 not in d2 == %d' % (1 not in d2,))
print('1 not in d3 == %d' % (1 not in d3,))
print('1 not in d4 == %d' % (1 not in d4,))
print('\'1\' not in d1 == %d' % ('1' not in d1,))
print('\'1\' not in d2 == %d' % ('1' not in d2,))
print('\'1\' not in d3 == %d' % ('1' not in d3,))
print('\'1\' not in d4 == %d' % ('1' not in d4,))


# iteration

for k in d1:
    print('d1[%d] == %d' % (k, d1[k]))
