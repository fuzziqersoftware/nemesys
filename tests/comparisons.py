def do_comparisons(a, b, check_is=True):
  print('  ' + repr(a) + ' < ' + repr(b) + ' == ' + repr(a < b))
  print('  ' + repr(a) + ' > ' + repr(b) + ' == ' + repr(a > b))
  print('  ' + repr(a) + ' <= ' + repr(b) + ' == ' + repr(a <= b))
  print('  ' + repr(a) + ' >= ' + repr(b) + ' == ' + repr(a >= b))
  print('  ' + repr(a) + ' == ' + repr(b) + ' == ' + repr(a == b))
  print('  ' + repr(a) + ' != ' + repr(b) + ' == ' + repr(a != b))
  if check_is:
    print('  ' + repr(a) + ' is ' + repr(b) + ' == ' + repr(a is b))
    print('  ' + repr(a) + ' is not ' + repr(b) + ' == ' + repr(a is not b))

bytes_aa = b'aa'
bytes_bb = b'bb'
bytes_aa_split = b'a' + b'a'
bytes_bb_split = b'b' + b'b'
unicode_aa = 'aa'
unicode_bb = 'bb'
unicode_aa_split = 'a' + 'a'
unicode_bb_split = 'b' + 'b'

# we don't run the is/is not operators because they produce different results in
# nemesys vs. cpython, and this is (currently) expected behavior

print('bytes_aa vs bytes_aa')
do_comparisons(bytes_aa, bytes_aa)
print('bytes_aa vs bytes_aa_split')
do_comparisons(bytes_aa, bytes_aa_split, check_is=False)
print('bytes_aa vs bytes_bb')
do_comparisons(bytes_aa, bytes_bb)
print('bytes_bb vs bytes_aa')
do_comparisons(bytes_bb, bytes_aa)

print('unicode_aa vs unicode_aa')
do_comparisons(unicode_aa, unicode_aa)
print('unicode_aa vs unicode_aa_split')
do_comparisons(unicode_aa, unicode_aa_split, check_is=False)
print('unicode_aa vs unicode_bb')
do_comparisons(unicode_aa, unicode_bb)
print('unicode_bb vs unicode_aa')
do_comparisons(unicode_bb, unicode_aa)

print('2 vs 3')
do_comparisons(2, 3)
#do_comparisons(2.0, 3)
#do_comparisons(2, 3.0)
#do_comparisons(2.0, 3.0)
