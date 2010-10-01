#!/usr/bin/python

# Some tests for Meego stacking layer support.

#* Test steps
#  * show an application window
#  * show an application window transient to the first one
#  * show an application window transient to the first transient
#  * show an application window transient to the second transient
#  * show an application window
#  * set the first non-transient application window to Meego level 1
#  * set the second non-transient application window to Meego level 1
#  * check correct stacking
#  * set the first non-transient application window to Meego level 2
#* Post-conditions
#  * check correct stacking

import os, re, sys, time

if os.system('mcompositor-test-init.py'):
  sys.exit(1)

# create application window
fd = os.popen('windowctl kn')
app1 = fd.readline().strip()
time.sleep(1)

# create transient application windows
fd = os.popen('windowctl kn %s' % app1)
trans1 = fd.readline().strip()
fd = os.popen('windowctl kn %s' % trans1)
trans2 = fd.readline().strip()
fd = os.popen('windowctl kn %s' % trans2)
trans3 = fd.readline().strip()
time.sleep(1)

# create application window
fd = os.popen('windowctl kn')
app2 = fd.readline().strip()
time.sleep(1)

# set the non-transient application windows to Meego level 1
os.popen('windowctl E %s 1' % app1)
os.popen('windowctl E %s 1' % app2)
time.sleep(1)

ret = app2_found = trans1_found = trans2_found = trans3_found = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % app2, l.strip()):
    print app2, 'found'
    app2_found = 1
  elif re.search("%s " % trans3, l.strip()) and app2_found:
    print trans3, 'found'
    trans3_found = 1
  elif re.search("%s " % trans2, l.strip()) and app2_found and trans3_found:
    print trans2, 'found'
    trans2_found = 1
  elif re.search("%s " % trans1, l.strip()) and app2_found and trans3_found \
     and trans2_found:
    print trans1, 'found'
    trans1_found = 1
  elif re.search("%s " % app1, l.strip()) and app2_found and trans1_found \
       and trans2_found and trans3_found:
    print app1, 'found'
    break
  elif not re.search(" no-TYPE ", l.strip()):
    print 'FAIL: stacking order is wrong'
    print 'Failed stack:\n', s
    ret = 1
    break

# set the first application window to Meego level 2
os.popen('windowctl E %s 2' % app1)
time.sleep(1)

app2_found = trans1_found = trans2_found = trans3_found = 0
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if re.search("%s " % trans3, l.strip()):
    print trans3, 'found'
    trans3_found = 1
  elif re.search("%s " % trans2, l.strip()) and trans3_found:
    print trans2, 'found'
    trans2_found = 1
  elif re.search("%s " % trans1, l.strip()) and trans3_found and trans2_found:
    print trans1, 'found'
    trans1_found = 1
  elif re.search("%s " % app1, l.strip()) and trans1_found \
       and trans2_found and trans3_found:
    print app1, 'found'
    break
  elif re.search("%s " % app2, l.strip()):
    print app2, 'found'
  elif not re.search(" no-TYPE ", l.strip()):
    print 'FAIL: stacking order is wrong'
    print 'Failed stack:\n', s
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
