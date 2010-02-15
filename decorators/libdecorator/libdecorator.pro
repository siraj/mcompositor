include(../../duiconfig.pri)
TEMPLATE = lib
DEPENDPATH += .
INCLUDEPATH += .
CONFIG += dui dll release
QT += opengl
TARGET = decorator

HEADERS += duiabstractdecorator.h
SOURCES += duiabstractdecorator.cpp


target.path=/usr/lib
INSTALLS += target
QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
