#!/usr/bin/python

# Check that synthetic VisibilityNotifys are generated.

#* Test steps
#  * show a non-decorated window
#  * verify that it's unobscured
#  * show a non-decorated window
#  * verify that the lower one is obscured and top one unobscured
#  * unmap the topmost window
#  * verify that the remaining window is unobscured
#  * show a decorated window
#  * verify that it's unobscured
#  * show a decorated window
#* Post-conditions
#  * verify that the lower decorated window is obscured

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

ret = 0
def check_visi(win, expect_str):
  global ret
  fd = os.popen('windowstack m')
  s = fd.read(5000)
  for l in s.splitlines():
    if re.search('%s ' % win, l.strip()):
      if re.search(expect_str, l.strip()):
        print '%s %s' % (win, expect_str)
      else:
        print 'FAIL: %s is not %s' % (win, expect_str)
        print 'Failed stack:\n', s
        ret = 1
      break

# map non-decorated window
fd = os.popen('windowctl kn')
win1 = fd.readline().strip()
time.sleep(2)
check_visi(win1, ' UNOBS. ')

# map non-decorated window
fd = os.popen('windowctl kn')
win2 = fd.readline().strip()
time.sleep(2)
check_visi(win1, ' OBS. ')
check_visi(win2, ' UNOBS. ')

# unmap topmost window
os.popen('windowctl U %s' % win2)
time.sleep(2)
check_visi(win1, ' UNOBS. ')

# map decorated window
fd = os.popen('windowctl n')
win3 = fd.readline().strip()
time.sleep(2)
check_visi(win3, ' UNOBS. ')

# map decorated window
fd = os.popen('windowctl n')
win4 = fd.readline().strip()
time.sleep(2)
check_visi(win3, ' OBS. ')
check_visi(win4, ' UNOBS. ')

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
