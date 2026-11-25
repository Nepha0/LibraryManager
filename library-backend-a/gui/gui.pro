# GUI 演示窗口（Qt Widgets，可视化验收用）
QT += core gui sql widgets
CONFIG += c++17
TEMPLATE = app
TARGET = backend_a_gui

INCLUDEPATH += $$PWD/../src $$PWD/../..
# 静态库固定输出在源码目录 src/ 下（src.pro DESTDIR=$$PWD），shadow build 下同样成立
LIBS += $$PWD/../src/libbackenda.a
PRE_TARGETDEPS += $$PWD/../src/libbackenda.a

SOURCES += main.cpp mainwindow.cpp
HEADERS += mainwindow.h
