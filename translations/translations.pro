LANGUAGES = # empty, means to build only engineering English
SOURCEDIR = $$PWD/..
CATALOGNAME = recovery
TRANSLATIONDIR = $$PWD
TRANSLATION_INSTALLDIR = $$M_TRANSLATION_DIR
include($$[QT_INSTALL_DATA]/mkspecs/features/meegotouch_defines.prf)
include($$[QT_INSTALL_DATA]/mkspecs/features/meegotouch_translations.prf)
