include(../meegotouch_config.pri)

contains(QT_CONFIG, opengles2) {
     message("building Makefile for EGL/GLES2 version")
     SOURCES += mtexturepixmapitem_egl.cpp mcompositewindowgroup.cpp
     HEADERS += mcompositewindowgroup.h
     publicHeaders.files +=  mcompositewindowgroup.h
} else {
     # Qt wasn't built with EGL/GLES2 support but EGL is present
     # ensure we still use the EGL back-end 
     exists($$QMAKE_INCDIR_OPENGL/EGL) {
         message("building Makefile for EGL/GLES2 version")
         SOURCES += mtexturepixmapitem_egl.cpp
         LIBS += -lEGL
     } 
     # Otherwise use GLX backend
     else {
         message("building Makefile for GLX version")
         SOURCES += mtexturepixmapitem_glx.cpp
     }
} 

TEMPLATE = lib
TARGET = mcompositor
DEPENDPATH += .
QT += dbus

QMAKE_CXXFLAGS = -lX11 -lXext
LIBS += -lX11 -lXext
# Input
INCLUDEPATH += ../decorators/libdecorator/
HEADERS += \
    mtexturepixmapitem.h \
    mtexturepixmapitem_p.h \
    mcompositescene.h \
    mcompositewindow.h \
    mwindowpropertycache.h \
    mcompwindowanimator.h \
    mcompositemanager.h \
    msimplewindowframe.h \
    mcompositemanager_p.h \
    mcompatoms_p.h \
    mdecoratorframe.h \
    mcompositemanagerextension.h \
    mcompositewindowshadereffect.h \
    mcompmgrextensionfactory.h

SOURCES += \
    mtexturepixmapitem_p.cpp \
    mcompositescene.cpp \
    mcompositewindow.cpp \
    mwindowpropertycache.cpp \
    mcompwindowanimator.cpp \
    mcompositemanager.cpp \
    msimplewindowframe.cpp \
    mdecoratorframe.cpp \
    mcompositemanagerextension.cpp \
    mcompositewindowshadereffect.cpp

RESOURCES = tools.qrc

CONFIG += release link_pkgconfig
PKGCONFIG += contextsubscriber-1.0
QT += core gui opengl

# TODO: refactor the headers to exclude private stuff
publicHeaders.files += mcompositewindow.h \
                      mcompositemanager.h \
                      mcompositewindowshadereffect.h \
                      mcompositemanagerextension.h \
                      mwindowpropertycache.h \
                      mcompatoms_p.h \
                      mcompmgrextensionfactory.h
publicHeaders.path = $$M_INSTALL_HEADERS/mcompositor
INSTALLS += publicHeaders

target.path += /usr/lib
INSTALLS += target 

LIBS += -lXdamage -lXcomposite -lXfixes -lX11-xcb -lxcb-render -lxcb-shape \
        -lXrandr ../decorators/libdecorator/libdecorator.so

QMAKE_EXTRA_TARGETS += check
check.depends = $$TARGET
check.commands = $$system(true)
