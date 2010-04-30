include(../meegotouch_config.pri)

contains(QT_CONFIG, opengles2) {
     message("building Makefile for EGL/GLES2 version")
     DEFINES += GLES2_VERSION
     SOURCES += mtexturepixmapitem_egl.cpp
} else {
     # Qt wasn't built with EGL/GLES2 support but EGL is present
     # ensure we still use the EGL back-end 
     exists($$QMAKE_INCDIR_OPENGL/EGL) {
         message("building Makefile for EGL/GLES2 version")
         DEFINES += GLES2_VERSION
         SOURCES += mtexturepixmapitem_egl.cpp
         LIBS += -lEGL
     } 
     # Otherwise use GLX backend
     else {
         message("building Makefile for GLX version")
         DEFINES += DESKTOP_VERSION
         SOURCES += mtexturepixmapitem_glx.cpp
     }
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
    mdevicestate.h \
    mdecoratorframe.h

SOURCES += \
    main.cpp \
    mtexturepixmapitem_p.cpp \
    mcompositescene.cpp \
    mcompositewindow.cpp \
    mcompwindowanimator.cpp \
    mcompositemanager.cpp \
    msimplewindowframe.cpp \
    mdevicestate.cpp \
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
