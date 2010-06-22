#!/usr/bin/python

# Check that unmapping a background application does not cause other
# changes in the window stack

#* Test steps
#  * show an application window
#  * show another application window
#  * raise the desktop
#  * unmap the application that was opened last
#* Post-conditions
#  * remaining application is still stacked below the desktop

import os, re, sys, time

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

# create two application windows
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl n')
new_win = fd.readline().strip()
time.sleep(1)

# raise home
os.popen('windowctl A %s' % home_win)
time.sleep(1)

# unmap the last application
os.popen('windowctl U %s' % new_win)
time.sleep(1)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % home_win, l.strip()):
    print home_win, 'found'
    break
  elif re.search("%s " % old_win, l.strip()):
    print 'FAIL: old_win is stacked on top'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % new_win, l.strip()):
    print 'FAIL: new_win did not close'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
