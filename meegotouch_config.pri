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

# Compositor components only
VERSION = 0.5.5
