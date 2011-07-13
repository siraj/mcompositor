TEMPLATE = app
DEPENDPATH += .
INCLUDEPATH += . ../libdecorator
CONFIG += release
QT += opengl
QMAKE_CXXFLAGS = -lX11 -lXext

LIBS += ../libdecorator/libdecorator.so

SOURCES += main.cpp \
           qtdecotator.cpp \
	   windowframe.cpp

HEADERS += qtdecotator.h \
           windowframe.h

QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
target.path = /usr/bin
INSTALLS += target
