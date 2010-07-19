#!/usr/bin/python

# Check that ARGB windows can be shown.

#* Test steps
#  * create and show ARGB application window
#  * create and show ARGB dialog window
#* Post-conditions
#  * both windows are mapped

import os, re, sys, time

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

fd = os.popen('windowctl an')
old_win = fd.readline().strip()
fd = os.popen('windowctl ad')
new_win = fd.readline().strip()
time.sleep(1)

found = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % old_win, l.strip()) or \
     re.search("%s " % new_win, l.strip()):
    found += 1

ret = 0
if found != 2:
  print 'FAIL: didn\'t find two ARGB windows'
  print 'Failed stack:\n', s
  ret = 1

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
