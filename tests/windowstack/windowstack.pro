TEMPLATE = app
TARGET = windowstack

target.path=/usr/bin
CONFIG += meegotouch

DEPENDPATH += .
INCLUDEPATH += . 

LIBS += -lXcomposite
SOURCES += windowstack.cpp 

INSTALLS += target
