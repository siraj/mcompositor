#!/usr/bin/python
# Common initialisation commands for each test.

import os, sys, re

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled false'):
  print 'cannot disable notifications'

# Find and unmap all existing notification windows, so they don't get in our way.
r = re.compile("^(0x[0-9a-fA-F]+) NOTIFICATION ")
for win in [ m.groups(1)[0] for m in [ r.match(l) for l in os.popen("windowstack m") ] if m is not None ]:
	os.system('windowctl U %s' % win)

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

os.system('aegis-su -o com.nokia.maemo -n /usr/bin/windowstack')
os.system('aegis-su -o com.nokia.maemo -n /usr/bin/windowctl')

if os.system('windowstack m | grep mdecorator'):
  print 'mdecorator is not mapped!'
  sys.exit(1)

os.system('uptime')
