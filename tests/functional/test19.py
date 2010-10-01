#!/usr/bin/python

# Check that initial_state==IconicState windows can be raised/activated.

#* Test steps
#  * show an initial_state==IconicState application window
#  * activate it
#  * check that it's on top
#  * close it
#  * show an initial_state==IconicState application window
#  * raise it
#* Post-conditions
#  * check that it's on top

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

fd = os.popen('windowstack m')
s = fd.read(5000)
win_re = re.compile('^0x[0-9a-f]+')
home_win = 0
for l in s.splitlines():
  if re.search(' DESKTOP viewable ', l.strip()):
    home_win = win_re.match(l.strip()).group()

if home_win == 0:
  print 'FAIL: desktop window not found'
  sys.exit(1)

# create minimised application window
fd = os.popen('windowctl Ikn')
app_win = fd.readline().strip()
time.sleep(2)

# activate it
os.popen('windowctl A %s' % app_win)
time.sleep(2)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % app_win, l.strip()):
    print app_win, 'found'
    break
  elif re.search("%s " % home_win, l.strip()):
    print 'FAIL: activation failed'
    print 'Failed stack:\n', s
    ret = 1
    break

# close it
os.popen('pkill windowctl')
time.sleep(1)

# create minimised application window
fd = os.popen('windowctl Ikn')
app_win = fd.readline().strip()
time.sleep(2)

# raise it
os.popen('windowctl V %s None' % app_win)
time.sleep(2)

fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % app_win, l.strip()):
    print app_win, 'found'
    break
  elif re.search("%s " % home_win, l.strip()):
    print 'FAIL: raising failed'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
