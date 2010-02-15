DUICOMPOSITORSRCDIR = ../../src
INCLUDEPATH += . $$DUICOMPOSITORSRCDIR
DEPENDPATH = $$INCLUDEPATH
CONFIG += debug dui
TEMPLATE = app
DEFINES += UNIT_TEST

SOURCES += $$DUICOMPOSITORSRCDIR/duicompositemanager.cpp \
	$$DUICOMPOSITORSRCDIR/duicompositescene.cpp \
	$$DUICOMPOSITORSRCDIR/duicompositewindow.cpp \
	$$DUICOMPOSITORSRCDIR/duicompwindowanimator.cpp \
	$$DUICOMPOSITORSRCDIR/duisimplewindowframe.cpp \
	$$DUICOMPOSITORSRCDIR/duitexturepixmapitem.cpp \
	$$DUICOMPOSITORSRCDIR/duidecoratorframe.cpp

HEADERS += $$DUICOMPOSITORSRCDIR/duicompositemanager.h \
	$$DUICOMPOSITORSRCDIR/duicompositemanager_p.h \
	$$DUICOMPOSITORSRCDIR/duicompositescene.h \
	$$DUICOMPOSITORSRCDIR/duicompositewindow.h \
	$$DUICOMPOSITORSRCDIR/duicompwindowanimator.h \
	$$DUICOMPOSITORSRCDIR/duisimplewindowframe.h \
	$$DUICOMPOSITORSRCDIR/duitexturepixmapitem.h \
	$$DUICOMPOSITORSRCDIR/duidecoratorframe.h


INSTALLS += target
target.path = /usr/lib/duicompositor-tests

QT += testlib core gui opengl

!contains(QT_CONFIG, opengles2):DEFINES +=  DESKTOP_VERSION

LIBS += -lXdamage -lXcomposite -lXfixes


CONFIG += link_prl
QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = @LD_LIBRARY_PATH=../../lib:$$(LD_LIBRARY_PATH) ./$$TARGET
