#!/usr/bin/python

# Check that windows belonging to the same group are stacked together.

#* Test steps
#  * show two application windows in the same group
#  * show another application window that is not in the group
#  * raise the desktop
#  * raise the first grouped application window
#  * raise the non-grouped application window
#  * raise the second grouped application window
#* Post-conditions
#  * grouped application windows are stacked together above the desktop

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

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

# create two application windows in the same group
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(1)
os.popen('windowctl G %s %s' % (old_win, old_win))
time.sleep(1)
fd = os.popen('windowctl n')
new_win = fd.readline().strip()
time.sleep(1)
os.popen('windowctl G %s %s' % (new_win, old_win))
time.sleep(1)
# create one application window not in the group
fd = os.popen('windowctl n')
outsider = fd.readline().strip()
time.sleep(1)

# raise home
os.popen('windowctl A %s' % home_win)
time.sleep(1)

# raise first grouped application
os.popen('windowctl A %s' % old_win)
time.sleep(1)
# raise outsider
os.popen('windowctl A %s' % outsider)
time.sleep(1)
# raise second grouped application
os.popen('windowctl A %s' % new_win)
time.sleep(1)

ret = new_win_found = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % new_win, l.strip()):
    print new_win, '(new_win) found'
    new_win_found = 1
  elif re.search("%s " % old_win, l.strip()) and new_win_found:
    print old_win, '(old_win) found'
    break
  elif re.search("%s " % old_win, l.strip()):
    print 'FAIL: wrong app window is stacked on top'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % home_win, l.strip()):
    print 'FAIL: home is stacked on top'
    print 'Failed stack:\n', s
    ret = 1
    break
  elif re.search("%s " % outsider, l.strip()):
    print 'FAIL: non-grouped app is stacked before the group'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
