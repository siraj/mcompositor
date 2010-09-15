#!/usr/bin/python

# Check that TYPE_INPUT window is stacked above applications and dialogs.

#* Test steps
#  * create and show TYPE_INPUT window
#  * create and show application window
#  * create and show dialog window
#* Post-conditions
#  * TYPE_INPUT window is stacked above application and dialog windows.

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled false'):
  print 'cannot disable notifications'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

# create two dialogs
fd = os.popen('windowctl i')
input_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl d')
new_win = fd.readline().strip()
time.sleep(2)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % input_win, l.strip()):
    print input_win, 'found'
    break
  elif re.search("%s " % old_win, l.strip()):
    print 'FAIL: app is stacked before input'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % new_win, l.strip()):
    print 'FAIL: dialog is stacked before input'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
