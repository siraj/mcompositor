TEMPLATE = subdirs
CONFIG+=ordered
SUBDIRS = \
    decorators \
    src \
    mcompositor\
    tests \
    translations

src.depends=decorators

QMAKE_CLEAN += \ 
	configure-stamp \
	build-stamp \

QMAKE_DISTCLEAN += \
    configure-stamp \
    build-stamp \

check.target = check
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
