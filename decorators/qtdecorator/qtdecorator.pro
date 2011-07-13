include(../../meegotouch_config.pri)
TEMPLATE = app
DEPENDPATH += .
INCLUDEPATH += . ../libdecorator
CONFIG += release
QT += opengl
QMAKE_CXXFLAGS = -lX11 -lXext

LIBS += ../libdecorator/libdecorator.so
LIBS += -lX11 -lXext

SOURCES += main.cpp qtdecorator.cpp windowframe.cpp
HEADERS += windowframe.h qtdecorator.h

QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
target.path = /usr/bin
INSTALLS += target
