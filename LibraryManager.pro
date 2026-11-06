QT += core gui widgets sql
CONFIG += c++17
TEMPLATE = app
TARGET = LibraryManager
INCLUDEPATH += $$PWD
SOURCES += app/main.cpp \
           app/mainwindow.cpp \
           common/database.cpp \
           auth/authservice.cpp \
           books/bookservice.cpp \
           readers/readerservice.cpp \
           circulation/circulationservice.cpp \
           statistics/statisticsservice.cpp
HEADERS += app/mainwindow.h \
           common/database.h \
           auth/authservice.h \
           books/bookservice.h \
           readers/readerservice.h \
           circulation/circulationservice.h \
           statistics/statisticsservice.h
RESOURCES += resources/resources.qrc
