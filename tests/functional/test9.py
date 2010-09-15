#!/usr/bin/python

# Check that configure requests for changing stacking order are supported.

#* Test steps
#  * show an application window
#  * show another application window
#  * configure topmost application below the other
#  * configure bottommost application above the other
#  * configure bottommost application to the top (sibling None)
#* Post-conditions
#  * stacking order is changed according to the configure requests

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled false'):
  print 'cannot disable notifications'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

# create two application windows
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(2)
fd = os.popen('windowctl n')
new_win = fd.readline().strip()
time.sleep(2)

# stack top application below the other
os.popen('windowctl L %s %s' % (new_win, old_win))
time.sleep(2)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % old_win, l.strip()):
    print old_win, 'found'
    break
  elif re.search("%s " % new_win, l.strip()):
    print 'FAIL: configuring below failed'
    print 'Failed stack:\n', s
    ret = 1
    break

# stack bottom application above the other
os.popen('windowctl V %s %s' % (new_win, old_win))
time.sleep(2)

fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % new_win, l.strip()):
    print new_win, 'found'
    break
  elif re.search("%s " % old_win, l.strip()):
    print 'FAIL: configuring above failed'
    print 'Failed stack:\n', s
    ret = 1
    break

# stack bottom application to top
os.popen('windowctl V %s None' % old_win)
time.sleep(2)

fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % old_win, l.strip()):
    print old_win, 'found'
    break
  elif re.search("%s " % new_win, l.strip()):
    print 'FAIL: configuring to top failed'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
