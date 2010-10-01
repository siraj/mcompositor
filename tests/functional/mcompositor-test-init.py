#!/usr/bin/python
# Common initialisation commands for each test.

import os, sys

if os.system('/sbin/mcetool --unblank-screen --set-tklock-mode=unlocked --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('/usr/bin/gconftool-2 --type bool --set /desktop/meego/notifications/previews_enabled false'):
  print 'cannot disable notifications'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

os.system('aegis-su -o com.nokia.maemo -n /usr/bin/windowstack')
os.system('aegis-su -o com.nokia.maemo -n /usr/bin/windowctl')

if os.system('windowstack m | grep mdecorator'):
  print 'mdecorator is not mapped!'
  sys.exit(1)

os.system('uptime')
