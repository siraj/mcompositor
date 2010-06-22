#!/usr/bin/python

# Check that we can't raise app over a modal, non-transient dialog.

#* Test steps
#  * create a modal, non-transient dialog
#  * activate application in background
#* Post-conditions
#  * dialog should be stacked above the application

import os, re, sys, time

os.system('/usr/share/meegotouch/testapps/testApp1 &')
time.sleep(2)

fd = os.popen('windowstack m')
s = fd.read(5000)
win_re = re.compile('^0x[0-9a-f]+')
testapp_win = 0
for l in s.splitlines():
  if re.search(' testApp1 ', l.strip()):
    testapp_win = win_re.match(l.strip()).group()
    break

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
  if re.search(' testApp1 ', l.strip()):
    print 'FAIL: dialog is not on top'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % new_win, l.strip()):
    print new_win, 'found'
    break

# cleanup
os.popen('pkill windowctl')
os.popen('pkill testApp1')
time.sleep(1)

sys.exit(ret)
