# GUI 演示窗口（Qt Widgets，可视化验收用）
QT += core gui sql widgets
CONFIG += c++17
TEMPLATE = app
TARGET = backend_a_gui

INCLUDEPATH += $$PWD/../src
LIBS += -L$$OUT_PWD/../src -lbackenda
PRE_TARGETDEPS += $$OUT_PWD/../src/libbackenda.a

SOURCES += main.cpp mainwindow.cpp
HEADERS += mainwindow.h
