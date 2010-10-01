#!/usr/bin/python

# Check that new dialog is stacked above old one.

#* Test steps
#  * create a non-transient dialog
#  * create another non-transient dialog
#* Post-conditions
#  * the new dialog should be stacked above the old dialog

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

# create two dialogs
fd = os.popen('windowctl d')
old_win = fd.readline().strip()
fd = os.popen('windowctl d')
new_win = fd.readline().strip()
time.sleep(4)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % old_win, l.strip()):
    print 'FAIL: old dialog is on top'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % new_win, l.strip()):
    print new_win, 'found'
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
