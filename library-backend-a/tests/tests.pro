# QTest 测试套件
QT += core gui sql testlib
greaterThan(QT_MAJOR_VERSION, 5): QT += gui-private
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = backend_a_tests

INCLUDEPATH += $$PWD/../src $$PWD/../.. $$PWD/../third_party/QXlsx
# 静态库固定输出在源码目录 src/ 下（src.pro DESTDIR=$$PWD），shadow build 下同样成立
LIBS += $$PWD/../src/libbackenda.a
PRE_TARGETDEPS += $$PWD/../src/libbackenda.a

SOURCES += test_backend_a.cpp
HEADERS += test_backend_a.h
