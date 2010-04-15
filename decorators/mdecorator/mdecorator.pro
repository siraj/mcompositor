include(../../meegotouch_config.pri)
TEMPLATE = app
DEPENDPATH += .
INCLUDEPATH += . ../libdecorator
CONFIG += meegotouch release
QT += opengl

LIBS += ../libdecorator/libdecorator.so

SOURCES += main.cpp mdecoratorwindow.cpp
HEADERS += mdecoratorwindow.h

QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
target.path = /usr/bin
INSTALLS += target
