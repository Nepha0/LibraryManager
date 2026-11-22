QT += core sql
QT -= gui

CONFIG += c++17 console
CONFIG -= app_bundle

TEMPLATE = app
TARGET = backend_b_smoke

INCLUDEPATH += ../..

SOURCES += main.cpp \
           ../../common/database.cpp \
           ../../auth/authservice.cpp \
           ../../auth/permissionservice.cpp \
           ../../common/paramservice.cpp \
           ../../common/operationlogservice.cpp

HEADERS += ../../common/database.h \
           ../../auth/authservice.h \
           ../../auth/permissionservice.h \
           ../../common/paramservice.h \
           ../../common/operationlogservice.h