# QTest 风格冒烟测试（无需数据库：未登录/缺参路径在触库前即返回）
QT += core sql testlib
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = backend_c_tests

INCLUDEPATH += $$PWD/../src $$PWD/../.. $$PWD/../../library-backend-a/src
LIBS += $$PWD/../src/libbackendc.a
PRE_TARGETDEPS += $$PWD/../src/libbackendc.a

SOURCES += test_backend_c.cpp
