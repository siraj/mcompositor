#!/usr/bin/python

# Check that initial_state=IconicState is respected in WM_HINTS

#* Test steps
#  * show an application window with initial_state=IconicState
#* Post-conditions
#  * application is stacked under the desktop window

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
    break

if home_win == 0:
  print 'FAIL: desktop not found'
  sys.exit(1)

# create initially iconic application window
fd = os.popen('windowctl In')
old_win = fd.readline().strip()
time.sleep(2)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % home_win, l.strip()):
    print home_win, 'found'
    break
  elif re.search("%s " % old_win, l.strip()):
    print 'FAIL: application window is not iconic'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
