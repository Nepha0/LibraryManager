QT += core sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TEMPLATE = app
TARGET = backend_b_smoke

INCLUDEPATH += $$PWD/../..
include(../backend-b.pri)

SOURCES += $$PWD/main.cpp \
           $$PWD/../../common/database.cpp \
           $$PWD/../../circulation/circulationservice.cpp \
           $$PWD/../../statistics/statisticsservice.cpp \
           $$PWD/../../books/demobookservice.cpp

HEADERS += $$PWD/../../common/database.h \
           $$PWD/../../circulation/circulationservice.h \
           $$PWD/../../statistics/statisticsservice.h \
           $$PWD/../../books/bookservice.h
