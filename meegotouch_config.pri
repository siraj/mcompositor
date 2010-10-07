# Load more defines from the dui_defines...
load(meegotouch_defines)

# Add global libdui includes
INCLUDEPATH += $$M_INSTALL_HEADERS

# Check for testability features, should they be compiled in or not ?

isEqual(TESTABILITY,"on") {
    DEFINES += WINDOW_DEBUG
}

# Check for Intel Meego
exists(/usr/include/X11/extensions/shapeconst.h) {
    exists(/usr/include/X11/extensions/shape.h) {
        DEFINES += NO_SHAPECONST
    } else {
        DEFINES += HAVE_SHAPECONST
    }
}

contains(QT_CONFIG, opengles2) {
     DEFINES += GLES2_VERSION
} else {
     # Qt wasn't built with EGL/GLES2 support but EGL is present
     # ensure we still use the EGL back-end 
     exists($$QMAKE_INCDIR_OPENGL/EGL) {
         DEFINES += GLES2_VERSION
     } 
     # Otherwise use GLX backend
     else {
         DEFINES += DESKTOP_VERSION
     }
} 

# Compositor components only
VERSION = 0.7.4
