CONFIG += staticlib
TEMPLATE = lib
TARGET = Common
SOURCES += SettingsNames.cpp \
    SetCoreApplication.cpp
HEADERS += SettingsNames.h \
    SetCoreApplication.cpp \
    SqlTransactionAutoAborter.h \
    PortNumbers.h

GITVERSION_PREFIX = $$join(PWD,,,/../..)
include(../gitversion.pri)
