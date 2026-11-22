QT += core gui widgets sql
CONFIG += c++17
TEMPLATE = app
TARGET = LibraryManager
INCLUDEPATH += $$PWD
SOURCES += app/main.cpp \
           app/mainwindow.cpp \
           auth/permissionservice.cpp \
           common/database.cpp \
           auth/authservice.cpp \
           books/bookservice.cpp \
           common/operationlogservice.cpp \
           common/paramservice.cpp \
           readers/readerservice.cpp \
           circulation/circulationservice.cpp \
           statistics/statisticsservice.cpp
HEADERS += app/mainwindow.h \
           auth/permissionservice.h \
           common/database.h \
           auth/authservice.h \
           books/bookservice.h \
           common/operationlogservice.h \
           common/paramservice.h \
           readers/readerservice.h \
           circulation/circulationservice.h \
           statistics/statisticsservice.h
RESOURCES += resources/resources.qrc
