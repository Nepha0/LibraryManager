# QTest 测试套件
QT += core gui sql testlib
greaterThan(QT_MAJOR_VERSION, 5): QT += gui-private
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = backend_a_tests

INCLUDEPATH += $$PWD/../src $$PWD/../src/third_party/QXlsx
LIBS += -L$$OUT_PWD/../src -lbackenda
PRE_TARGETDEPS += $$OUT_PWD/../src/libbackenda.a

SOURCES += test_backend_a.cpp
