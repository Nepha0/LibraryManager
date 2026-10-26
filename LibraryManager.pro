QT += core gui widgets sql
CONFIG += c++17
TEMPLATE = app
TARGET = LibraryManager
INCLUDEPATH += $$PWD
SOURCES += app/main.cpp \
           app/mainwindow.cpp
HEADERS += app/mainwindow.h
# Add each module's source files here when ready. A coordinates changes.
