TEMPLATE = subdirs
CONFIG+=ordered
SUBDIRS = \
	src \
#    tests \
    decorators

QMAKE_CLEAN += \ 
	configure-stamp \
	build-stamp \

QMAKE_DISTCLEAN += \
    configure-stamp \
    build-stamp \

startup_script.path = /etc/X11/Xsession.post
startup_script.files = startup/14duicompositor
startup_script.command = $$INSTALL_PROGRAM

INSTALLS += startup_script

check.target = check
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
