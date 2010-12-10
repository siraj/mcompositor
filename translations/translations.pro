LANGUAGES = # empty, means to build only engineering English
SOURCEDIR = $$PWD/..
CATALOGNAME = recovery
TRANSLATIONDIR = $$PWD
TRANSLATION_INSTALLDIR = $$M_TRANSLATION_DIR
include($$[QT_INSTALL_DATA]/mkspecs/features/meegotouch_defines.prf)
include($$[QT_INSTALL_DATA]/mkspecs/features/meegotouch_translations.prf)

# Only depend on $$FILES, and do not regenerate recovery.ts all the times.
FILES = $$SOURCEDIR/decorators/mdecorator/mdecoratorwindow.cpp
updateeets.input = FILES
QMAKE_EXTRA_COMPILERS -= dummy
