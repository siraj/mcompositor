# Load more defines from the dui_defines...
load(dui_defines)

# Add global libdui includes
INCLUDEPATH += $$DUI_INSTALL_HEADERS

# Check for testability features, should they be compiled in or not ?

isEqual(TESTABILITY,"on") {
    DEFINES += WINDOW_DEBUG
}
