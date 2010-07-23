#!/usr/bin/python

# Check that selective compositing works in most important cases.

#* Test steps
#  * start test app
#  * check that test app is not composited
#  * show a normal (non-fullscreen) application window
#  * check that application is composited (due to the decorator)
#  * iconify the application by raising desktop
#  * check that desktop is not composited
#  * show a non-decorated RGBA application window
#* Post-conditions
#  * check that compositing is on (because the topmost window is RGBA)

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

fd = os.popen('windowctl kn')
app_win = fd.readline().strip()
time.sleep(2)

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

# check that test app is not redirected
ret = 0
fd = os.popen('windowstack v')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % app_win, l.strip()) and re.search(' dir.', l.strip()):
    break
  elif re.search("%s " % app_win, l.strip()) and \
       re.search(' redir.', l.strip()):
    print 'FAIL: test app is redirected'
    print 'Failed stack:\n', s
    ret = 1
    break

# map new decorated application window
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(2)
# check that the application is redirected
fd = os.popen('windowstack v')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % old_win, l.strip()) and re.search(' redir.', l.strip()):
    break
  elif re.search("%s " % old_win, l.strip()) and \
       re.search(' dir.', l.strip()):
    print 'FAIL: old_win is not redirected'
    print 'Failed stack:\n', s
    ret = 1
    break

# raise home
os.popen('windowctl A %s' % home_win)
time.sleep(3)
# check that duihome is not redirected
fd = os.popen('windowstack v')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % home_win, l.strip()) and re.search(' dir.', l.strip()):
    break
  elif re.search("%s " % home_win, l.strip()) and \
       re.search(' redir.', l.strip()):
    print 'FAIL: desktop is redirected'
    print 'Failed stack:\n', s
    ret = 1
    break

# map new non-decorated application with alpha
fd = os.popen('windowctl akn')
new_win = fd.readline().strip()
time.sleep(2)
# check that new_win is redirected
fd = os.popen('windowstack v')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % new_win, l.strip()) and re.search(' redir.', l.strip()):
    break
  elif re.search("%s " % new_win, l.strip()) and \
       re.search(' dir.', l.strip()):
    print 'FAIL: RGBA app is not redirected'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
