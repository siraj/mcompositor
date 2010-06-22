#!/usr/bin/python

# Background application raises on top when activated.

#* Test steps
#  * create new application window
#  * activate a background application
#* Post-conditions
#  * background application is stacked above the new application

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

# create new app window on top
fd = os.popen('windowctl n')
new_win = fd.readline().strip()
time.sleep(1)

# activate the one below
os.popen("windowctl A %s" % testapp_win)
time.sleep(1)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search(' testApp1 ', l.strip()):
    print 'testApp1 found'
    break
  elif re.search("%s " % new_win, l.strip()):
    print 'FAIL: activated app is not on top'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
os.popen('pkill testApp1')
time.sleep(1)

sys.exit(ret)
