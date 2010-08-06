#!/usr/bin/python

# Check that two transients on the same level maintain their order.
# (Written for NB#177840)

#* Test steps
#  * show an application window
#  * create and show a window that is transient for the application
#  * create and show a window that is transient for the application
#  * check that the transients have the right order
#  * iconify the application window
#  * activate the application window
#  * the transients have raised and maintained their mutual order
#  * iconify the application window
#  * activate the topmost transient window
#* Post-conditions
#  * the transients have raised and maintained their mutual order

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

ret = 0
def check_window_order():
  global ret, upper_dlg, lower_dlg, app_win, home_win
  upper_found = 0
  fd = os.popen('windowstack m')
  s = fd.read(5000)
  for l in s.splitlines():
    if re.search("%s " % upper_dlg, l.strip()):
      print upper_dlg, 'found'
      upper_found = 1
    elif re.search("%s " % lower_dlg, l.strip()) and upper_found:
      print lower_dlg, 'found'
      break
    elif re.search("%s " % lower_dlg, l.strip()):
      print 'FAIL: order of transients is wrong'
      print 'Failed stack:\n', s
      ret = 1
      break
    elif re.search("%s " % app_win, l.strip()):
      print 'FAIL: app is not stacked after the dialogs'
      print 'Failed stack:\n', s
      ret = 1
      break
    elif re.search("%s " % home_win, l.strip()):
      print 'FAIL: home is stacked before app'
      print 'Failed stack:\n', s
      ret = 1
      break

fd = os.popen('windowstack m')
s = fd.read(5000)
win_re = re.compile('^0x[0-9a-f]+')
home_win = 0
for l in s.splitlines():
  if re.search(' DESKTOP viewable ', l.strip()):
    home_win = win_re.match(l.strip()).group()

if home_win == 0:
  print 'FAIL: desktop window not found'
  sys.exit(1)

# create application and transient dialog windows
fd = os.popen('windowctl kn')
app_win = fd.readline().strip()
time.sleep(1)
fd = os.popen("windowctl kn %s" % app_win)
lower_dlg = fd.readline().strip()
time.sleep(1)
fd = os.popen("windowctl kn %s" % app_win)
upper_dlg = fd.readline().strip()
time.sleep(2)

check_window_order()

# iconify the application
os.popen("windowctl O %s" % app_win)
time.sleep(2)

# activate the application (this should raise the dialogs too)
os.popen("windowctl A %s" % app_win)
time.sleep(2)

check_window_order()

# iconify the application
os.popen("windowctl O %s" % app_win)
time.sleep(2)

# activate the topmost transient window
os.popen("windowctl A %s" % upper_dlg)
time.sleep(2)

check_window_order()

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
