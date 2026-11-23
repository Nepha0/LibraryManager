# 控制台演示程序（前端未就绪时验收用）
QT += core gui sql
greaterThan(QT_MAJOR_VERSION, 5): QT += gui-private
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = backend_a_demo

INCLUDEPATH += $$PWD/../src $$PWD/../third_party/QXlsx
# 静态库固定输出在源码目录 src/ 下（src.pro DESTDIR=$$PWD），shadow build 下同样成立
LIBS += $$PWD/../src/libbackenda.a
PRE_TARGETDEPS += $$PWD/../src/libbackenda.a

SOURCES += main.cpp
