#!/usr/bin/python

# Check that input focus is assigned correctly.

#* Test steps
#  * check that the desktop window has input focus
#  * show a decorated application window
#  * check that the window has input focus
#  * show a non-decorated application window
#  * check that the window has input focus
#  * show a non-decorated dialog window
#  * check that the dialog has input focus
#  * show a non-decorated application window that does not want focus
#  * check that the dialog has input focus
#  * unmap the dialog window
#  * check that the application window has the input focus
#  * unmap the application windows
#* Post-conditions
#  * check that the desktop window has input focus

import os, re, sys, time

ret = 0
def check_focus(w):
  global ret
  correct = 0
  fd = os.popen('focus-tracker -o')
  s = fd.read(5000)
  for l in s.splitlines():
    m = re.search('(?<=^Focused )0x[0-9a-f]+', l.strip()).group(0)
    if m == w:
      correct = 1
      break
  if correct == 0:
    print 'FAIL: window %s does not have focus:' % w
    print s
    fd = os.popen('windowstack m')
    print fd.read(5000)
    ret = 1

if os.system('/sbin/mcetool --unblank-screen --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled false'):
  print 'cannot disable notifications'

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

check_focus(home_win)

# create decorated application window
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(2)
check_focus(old_win)

# create non-decorated application window
fd = os.popen('windowctl kn')
new_win = fd.readline().strip()
time.sleep(2)
check_focus(new_win)

# create non-decorated dialog window
fd = os.popen('windowctl kd')
dialog = fd.readline().strip()
time.sleep(2)
check_focus(dialog)

# show a non-decorated application window that does not want focus
fd = os.popen('windowctl ckn')
no_focus = fd.readline().strip()
time.sleep(2)
check_focus(dialog)

# unmap the dialog
os.popen('windowctl U %s' % dialog)
time.sleep(1)
check_focus(new_win)

# close all application windows
os.popen('pkill windowctl')
time.sleep(1)
check_focus(home_win)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
