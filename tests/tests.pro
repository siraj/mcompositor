TEMPLATE    = subdirs
SUBDIRS = \
          ut_duisimplewindowframe \
          ut_duicompositescene

QMAKE_STRIP = echo

check.target = check
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check

QMAKE_CLEAN += **/*.log.xml ./coverage.log.xml **/*.log
