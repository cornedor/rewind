QT += core gui qml quick quickcontrols2 concurrent testlib
CONFIG += c++17 testcase
TARGET = rewind_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/backupservice.h ../src/files.h ../src/models.h ../src/overview.h ../src/repository.h \
           ../src/restic.h ../src/theme.h ../src/translator.h
SOURCES += rewind_tests.cpp ../src/backend.cpp ../src/backupservice.cpp ../src/files.cpp ../src/models.cpp ../src/overview.cpp \
           ../src/repository.cpp ../src/restic.cpp ../src/theme.cpp ../src/translator.cpp
