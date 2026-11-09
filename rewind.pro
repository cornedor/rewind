QT += core gui qml quick quickcontrols2 concurrent

CONFIG += c++17 release
TARGET = rewind
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/backupservice.h \
    src/cli.h \
    src/files.h \
    src/models.h \
    src/overview.h \
    src/repository.h \
    src/restic.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/backupservice.cpp \
    src/cli.cpp \
    src/files.cpp \
    src/models.cpp \
    src/overview.cpp \
    src/repository.cpp \
    src/restic.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
