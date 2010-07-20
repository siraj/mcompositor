#!/usr/bin/python

# Check that window stack stays static while rotating the screen.

#* Test steps
#  * create and show TYPE_INPUT window
#  * create and show application window
#  * create and show dialog window
#  * rotate screen step-by-step and check that window stack says the same
#* Post-conditions
#  * None

import os, re, sys, time

if os.system('/sbin/mcetool --unblank-screen --set-inhibit-mode=stay-on'):
  print 'mcetool is missing!'

if os.system('pidof mcompositor'):
  print 'mcompositor is not running'
  sys.exit(1)

def rotate_screen(top_edge):
  print 'rotate_screen:', top_edge
  os.popen("windowctl R %s" % top_edge)
  time.sleep(5)

def print_stack_array(a):
  i = 0
  while i < len(a):
    if a[i] == 'root':
      print a[i:i+5]
      break
    print a[i:i+4]
    i += 4

# create input, app, and dialog windows
fd = os.popen('windowctl i')
input_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl n')
old_win = fd.readline().strip()
time.sleep(1)
fd = os.popen('windowctl d')
new_win = fd.readline().strip()
time.sleep(2)

orig_stack = []
fd = os.popen('windowstack m')
s = fd.read(5000)
for l in s.splitlines():
  if l.split()[0] == 'root':
    print 'root found'
    orig_stack += l.strip().split()
    break
  if l.split()[1] != 'no-TYPE':
    orig_stack += l.strip().split()[0:4]

# rotate 90 degrees and check the stack
ret = 0
for arg in ['l', 'b', 'r', 't']:
  rotate_screen(arg)
  new_stack = []
  fd = os.popen('windowstack m')
  s = fd.read(5000)
  for l in s.splitlines():
    if l.split()[0] == 'root':
      new_stack += l.strip().split()
      break
    if l.split()[1] != 'no-TYPE':
      new_stack += l.strip().split()[0:4]
  if orig_stack != new_stack:
    print 'Failed stack:'
    print_stack_array(new_stack)
    print 'Original stack:'
    print_stack_array(orig_stack)
    ret = 1
    break

# cleanup
os.popen('pkill windowctl')
time.sleep(1)

sys.exit(ret)
