#!/usr/bin/python

# Check that TYPE_NOTIFICATION window is stacked above applications,
# dialogs and input windows.

#* Test steps
#  * create and show notification window
#  * create and show application window
#  * create and show dialog window
#  * create and show TYPE_INPUT window
#* Post-conditions
#  * Notification window is stacked above application, dialog and input
#    window

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

# create notification, app, dialog, and input windows
fd = os.popen('windowctl b')
note_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl d')
new_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl i')
input_win = fd.readline().strip()
time.sleep(2)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % note_win, l.strip()):
    print note_win, 'found'
    break
  elif re.search("%s " % old_win, l.strip()):
    print 'FAIL: app is stacked above notification'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % new_win, l.strip()):
    print 'FAIL: dialog is stacked above notification'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % input_win, l.strip()):
    print 'FAIL: input is stacked above notification'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
