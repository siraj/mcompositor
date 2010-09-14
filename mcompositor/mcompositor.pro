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

QT = gui opengl
