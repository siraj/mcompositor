include(../../duiconfig.pri)
TEMPLATE = app
DEPENDPATH += .
INCLUDEPATH += . ../libdecorator
CONFIG += dui release
QT += opengl

LIBS += ../libdecorator/libdecorator.so.1.0.0

SOURCES += main.cpp duidecoratorwindow.cpp
HEADERS += duidecoratorwindow.h

QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
target.path = /usr/bin
INSTALLS += target
