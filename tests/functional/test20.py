#!/usr/bin/python

# Check that jammed and (during call) fullscreen apps are decorated.

#* Test steps
#  * show an application window that does not respond to pings
#  * wait for 7 seconds to make compositor notice it jammed
#  * check that the decorator is on top of it
#  * close it
#  * simulate an ongoing call
#  * show a fullscreen application window
#* Post-conditions
#  * check that the decorator is on top of it

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

fd = os.popen('windowstack m')
s = fd.read(5000)
win_re = re.compile('^0x[0-9a-f]+')
deco_win = 0
for l in s.splitlines():
  if re.search(' viewable mdecorator', l.strip()):
    deco_win = win_re.match(l.strip()).group()

if deco_win == 0:
  print 'FAIL: decorator window not found'
  sys.exit(1)

# create application window that does not respond to pings
fd = os.popen('windowctl pkn')
app_win = fd.readline().strip()
time.sleep(7)

ret = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % deco_win, l.strip()):
    print deco_win, 'found'
    break
  elif re.search("%s " % app_win, l.strip()):
    print 'FAIL: decorator is below the application'
    print 'Failed stack:\n', s
    ret = 1
    break

# close it
os.popen('pkill windowctl')
time.sleep(1)

# simulate a phone call
fd = os.popen('windowctl P')
time.sleep(1)

# create a fullscreen application window
fd = os.popen('windowctl fn')
app_win = fd.readline().strip()
time.sleep(2)

fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % deco_win, l.strip()):
    print deco_win, 'found'
    break
  elif re.search("%s " % app_win, l.strip()):
    print 'FAIL: decorator is below the application'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
os.popen('pkill context-provide')
time.sleep(1)

sys.exit(ret)
