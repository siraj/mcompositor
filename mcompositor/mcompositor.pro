include(../meegotouch_config.pri)

TEMPLATE = app
TARGET = 
DEPENDPATH += .
INCLUDEPATH += ../src

LIBS += ../src/libmcompositor.so ../decorators/libdecorator/libdecorator.so

target.path += $$M_INSTALL_BIN
INSTALLS += target 

# Input
SOURCES += main.cpp

contains(DEFINES, WINDOW_DEBUG) {
    HEADERS += xserverpinger.h
    SOURCES += xserverpinger.cpp
}

QT = core gui opengl
