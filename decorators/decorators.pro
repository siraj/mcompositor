TEMPLATE = subdirs
#temporary disable
CONFIG+=ordered
SUBDIRS += \
    sub_lib \
    sub_decorator \

sub_lib.subdir=libdecorator

sub_decorator.depends=sub_lib
sub_decorator.subdir=duidecorator


check.target = check 
check.CONFIG = recursive
QMAKE_EXTRA_TARGETS += check
