# 控制台演示程序（前端未就绪时验收用）
QT += core gui sql
greaterThan(QT_MAJOR_VERSION, 5): QT += gui-private
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = backend_a_demo

INCLUDEPATH += $$PWD/../src $$PWD/../src/third_party/QXlsx
LIBS += -L$$OUT_PWD/../src -lbackenda
PRE_TARGETDEPS += $$OUT_PWD/../src/libbackenda.a

SOURCES += main.cpp
