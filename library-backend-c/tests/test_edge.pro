# 边界/极端/冲突场景测试（独立于冒烟测试运行）
QT += core sql
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = backend_c_edge_tests

INCLUDEPATH += $$PWD/../src $$PWD/../.. $$PWD/../../library-backend-a/src
LIBS += $$PWD/../src/libbackendc.a
PRE_TARGETDEPS += $$PWD/../src/libbackendc.a

SOURCES += test_backend_c_edge.cpp
