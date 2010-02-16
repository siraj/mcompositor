TEMPLATE = subdirs
CONFIG+=ordered
SUBDIRS = \
	src \
    decorators

QMAKE_CLEAN += \ 
	configure-stamp \
	build-stamp \

QMAKE_DISTCLEAN += \
    configure-stamp \
    build-stamp \

check.target = check
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
