TEMPLATE = app
TARGET = windowstack

target.path=/usr/bin

DEPENDPATH += .
INCLUDEPATH += . 

LIBS += -lXcomposite
SOURCES += windowstack.cpp 

INSTALLS += target
