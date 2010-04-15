include(../meegotouch_config.pri)

contains(QT_CONFIG, opengles2) {
     DEFINES += GLES2_VERSION
     SOURCES += mtexturepixmapitem_egl.cpp
} else {
     DEFINES += DESKTOP_VERSION
     SOURCES += mtexturepixmapitem_glx.cpp
} 

# This is not a DUI prefix
exists(/usr/include/X11/extensions/shapeconst.h) {
     DEFINES += HAVE_SHAPECONST
}

TEMPLATE = app
TARGET = mcompositor
DEPENDPATH += .
QT += dbus

# Input
INCLUDEPATH += ../decorators/libdecorator/
HEADERS += \
    mtexturepixmapitem.h \
    mcompositescene.h \
    mcompositewindow.h \
    mcompwindowanimator.h \
    mcompositemanager.h \
    msimplewindowframe.h \
    mcompositemanager_p.h \
    mdecoratorframe.h

SOURCES += \
    main.cpp \
    mtexturepixmapitem_p.cpp \
    mcompositescene.cpp \
    mcompositewindow.cpp \
    mcompwindowanimator.cpp \
    mcompositemanager.cpp \
    msimplewindowframe.cpp \
    mdecoratorframe.cpp

RESOURCES = tools.qrc

CONFIG +=  release
QT += core gui opengl

target.path += $$M_INSTALL_BIN
INSTALLS += target 

LIBS += -lXdamage -lXcomposite -lXfixes ../decorators/libdecorator/libdecorator.so

QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
