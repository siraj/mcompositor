TEMPLATE = app
TARGET = windowctl

target.path=/usr/bin

QMAKE_CXXFLAGS+= -Wall
QMAKE_CFLAGS+= -Wall

LIBS+=-lX11 -lXrender -lXdamage
CONFIG += meegotouch

DEPENDPATH += .
INCLUDEPATH += .  

SOURCES += windowctl.cpp

INSTALLS +=  \
        target
