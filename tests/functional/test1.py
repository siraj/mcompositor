#!/usr/bin/python

# Check that we can't raise app over a modal, non-transient dialog.

#* Test steps
#  * create a modal, non-transient dialog
#  * activate application in background
#* Post-conditions
#  * dialog should be stacked above the application

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

fd = os.popen('windowctl kn')
testapp_win = fd.readline().strip()
time.sleep(2)

# create new dialog window on top
fd = os.popen('windowctl md')
new_win = fd.readline().strip()
time.sleep(1)

# check that we can't raise app over the dialog
os.popen("windowctl A %s" % testapp_win)
time.sleep(1)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search('%s ' % testapp_win, l.strip()):
    print 'FAIL: dialog is not on top'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % new_win, l.strip()):
    print new_win, 'found'
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
