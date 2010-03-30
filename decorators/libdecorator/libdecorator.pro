include(../../duiconfig.pri)
TEMPLATE = lib
DEPENDPATH += .
INCLUDEPATH += .
CONFIG += dll release
QT += opengl network
TARGET = decorator
VERSION = 0.3.7

HEADERS += duiabstractdecorator.h \
           duirmiclient_p.h \
           duirmiclient.h \
           duirmiserver_p.h \
           duirmiserver.h
SOURCES += duiabstractdecorator.cpp \
           duirmiclient.cpp \
           duirmiserver.cpp

target.path=/usr/lib
INSTALLS += target
QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
