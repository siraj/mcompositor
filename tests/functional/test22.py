#!/usr/bin/python

# Some tests for correct WM_STATE setting (Qt is very fond of it).

#* Test steps
#  * map an application window
#  * check that it is in Normal state
#  * minimise the application window
#  * check that it is in Iconic state
#  * maximise the application window
#  * check that it is in Normal state
#  * unmap the application window
#  * check that it is in Withdrawn state
#  * map the application window
#* Post-conditions
#  * check that it is in Normal state and stacked on top

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

if home_win == 0:
  print 'FAIL: desktop not found'
  sys.exit(1)

# create an application window
fd = os.popen('windowctl kne')
app = fd.readline().strip()
time.sleep(1)

ret = 0
fd = os.popen("xprop -id %s | grep 'window state' | awk '{print $3}'" % app)
if fd.readline().strip() != 'Normal':
  print 'FAIL: app is not in Normal state after mapping it'
  ret = 1

# minimise the application window
os.popen("windowctl A %s" % home_win)
time.sleep(1)

fd = os.popen("xprop -id %s | grep 'window state' | awk '{print $3}'" % app)
if fd.readline().strip() != 'Iconic':
  print 'FAIL: app is not in Iconic state'
  ret = 1

# maximise the application window
os.popen("windowctl A %s" % app)
time.sleep(1)

fd = os.popen("xprop -id %s | grep 'window state' | awk '{print $3}'" % app)
if fd.readline().strip() != 'Normal':
  print 'FAIL: app is not in Normal state after maximising it'
  ret = 1

# unmap the application window
os.popen("windowctl U %s" % app)
time.sleep(1)

fd = os.popen("xprop -id %s | grep 'window state' | awk '{print $3}'" % app)
if fd.readline().strip() != 'Withdrawn':
  print 'FAIL: app is not in Withdrawn state after unmapping it'
  ret = 1

# map the application window
os.popen("windowctl M %s" % app)
time.sleep(1)

fd = os.popen("xprop -id %s | grep 'window state' | awk '{print $3}'" % app)
if fd.readline().strip() != 'Normal':
  print 'FAIL: app is not in Normal state after re-mapping it'
  ret = 1

# check the application window is on top
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search('%s ' % home_win, l.strip()):
    print 'FAIL: app is not on top'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % app, l.strip()):
    print app, 'found'
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
