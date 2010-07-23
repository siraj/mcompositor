#!/usr/bin/python

# Background application raises on top when activated.

#* Test steps
#  * create new application window
#  * activate a background application
#* Post-conditions
#  * background application is stacked above the new application

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

if os.system('windowstack m | grep mdecorator'):
  print 'mdecorator is not mapped!'
  sys.exit(1)

fd = os.popen('windowctl kn')
testapp_win = fd.readline().strip()
time.sleep(2)

# create new app window on top
fd = os.popen('windowctl n')
new_win = fd.readline().strip()
time.sleep(2)

# activate the one below
os.popen("windowctl A %s" % testapp_win)
time.sleep(4)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search('%s ' % testapp_win, l.strip()):
    print testapp_win, 'found'
    break
  elif re.search("%s " % new_win, l.strip()):
    print 'FAIL: activated app is not on top'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
