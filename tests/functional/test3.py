#!/usr/bin/python

# Check that duihome can be activated and "deactivated".

#* Test steps
#  * start test app
#  * activate duihome
#* Post-conditions
#  * duihome should be above test app

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

fd = os.popen('windowctl kn')
app_win = fd.readline().strip()
time.sleep(2)

fd = os.popen('windowstack m')
s = fd.read(5000)
win_re = re.compile('^0x[0-9a-f]+')
home_win = 0
for l in s.splitlines():
  if re.search(' DESKTOP viewable ', l.strip()):
    home_win = win_re.match(l.strip()).group()

if home_win == 0:
  print 'FAIL: desktop not found'
  sys.exit(1)

# activate home
os.popen("windowctl A %s" % home_win)
time.sleep(3)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % app_win, l.strip()):
    print 'FAIL: application is on top'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % home_win, l.strip()):
    print home_win, 'found'
    break

# deactivate home by activating the test application
os.popen("windowctl A %s" % app_win)
time.sleep(3)

fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % app_win, l.strip()):
    print app_win, 'found'
    break
  elif re.search("%s " % home_win, l.strip()):
    print 'FAIL: home is on top'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
