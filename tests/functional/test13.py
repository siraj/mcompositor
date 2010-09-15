#!/usr/bin/python

# Check that _NET_WM_STATE_FULLSCREEN in _NET_WM_STATE is respected.

#* Test steps
#  * show a non-fullscreen application window
#  * request _NET_WM_STATE _NET_WM_STATE_FULLSCREEN for the window
#* Post-conditions
#  * verify that the window now has fullscreen size

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled false'):
  print 'cannot disable notifications'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

def get_window_size(w):
  ret = [0, 0]
  size_re = re.compile('([0-9]+)x([0-9]+)x([0-9]+)')
  fd = os.popen('windowstack v')
  s = fd.read(5000)
  for l in s.splitlines():
    if re.search("%s " % w, l.strip()):
      ret[0] = int(size_re.search(l.strip()).group(1))
      ret[1] = int(size_re.search(l.strip()).group(2))
      break
  return ret

(fs_w, fs_h) = (864, 480)

# map non-fullscreen application window
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(1)
ret = 0
size = get_window_size(old_win)
if size[0] == fs_w and size[1] == fs_h:
  print 'FAIL: old_win is fullscreen'
  ret = 1

# fullscreen the application window
os.popen('windowctl F %s' % old_win)
time.sleep(1)
size = get_window_size(old_win)
if size[0] != fs_w or size[1] != fs_h:
  print 'FAIL: old_win is not fullscreen'
  ret = 1

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled true'):
  print 'cannot re-enable notifications'

sys.exit(ret)
