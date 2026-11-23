# 后端A 核心库（静态库 backenda）
QT += core gui sql
greaterThan(QT_MAJOR_VERSION, 5): QT += gui-private   # QXlsx 使用 QtGui 私有 API（zip）
CONFIG += c++17
TEMPLATE = lib
CONFIG += staticlib
TARGET = backenda
DESTDIR = $$PWD   # 静态库固定输出到 src/libbackenda.a，供 demo/tests 链接

INCLUDEPATH += $$PWD $$PWD/../third_party/QXlsx
DEFINES += QT_DEPRECATED_WARNINGS

HEADERS += \
    common/contract.h \
    database/DatabaseManager.h \
    services/search_core.h \
    services/BookService.h \
    services/CategoryService.h \
    services/CopyService.h \
    services/SearchService.h \
    services/AdvancedSearchService.h \
    services/ImportExportService.h

SOURCES += \
    database/DatabaseManager.cpp \
    services/BookService.cpp \
    services/CategoryService.cpp \
    services/CopyService.cpp \
    services/SearchService.cpp \
    services/AdvancedSearchService.cpp \
    services/ImportExportService.cpp

# QXlsx 第三方源码（免安装，见 third_party/QXlsx）
QXLSX_SOURCES = $$files($$PWD/../third_party/QXlsx/*.cpp)
QXLSX_HEADERS = $$files($$PWD/../third_party/QXlsx/*.h)
SOURCES += $$QXLSX_SOURCES
HEADERS += $$QXLSX_HEADERS
