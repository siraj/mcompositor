include(../../meegotouch_config.pri)
TEMPLATE = lib
DEPENDPATH += .
INCLUDEPATH += .
CONFIG += dll release
QT += opengl network
TARGET = decorator
QMAKE_CXXFLAGS = -lX11 -lXext

HEADERS += mabstractdecorator.h \
           mrmiclient_p.h \
           mrmiclient.h \
           mrmiserver_p.h \
           mrmiserver.h
SOURCES += mabstractdecorator.cpp \
           mrmiclient.cpp \
           mrmiserver.cpp

target.path=/usr/lib
INSTALLS += target
QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
