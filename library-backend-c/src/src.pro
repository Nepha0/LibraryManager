# 后端C 核心库（静态库 backendc）
# 复用共享 contract.h 和 A 端 DatabaseManager / CopyService（源码直编，免跨库链接顺序问题）
QT += core sql
CONFIG += c++17
TEMPLATE = lib
CONFIG += staticlib
TARGET = backendc
DESTDIR = $$PWD   # 静态库固定输出到 src/libbackendc.a，供 tests 链接

INCLUDEPATH += $$PWD $$PWD/../.. $$PWD/../../library-backend-a/src
DEFINES += QT_DEPRECATED_WARNINGS

HEADERS += \
    contract_c.h \
    ../../common/contract.h \
    ../../library-backend-a/src/database/DatabaseManager.h \
    ../../library-backend-a/src/services/CopyService.h \
    services/BorrowService.h \
    services/ReturnService.h \
    services/RenewService.h \
    services/ReservationService.h \
    services/OverdueService.h \
    services/PersonalService.h \
    services/RecommendService.h

SOURCES += \
    ../../library-backend-a/src/database/DatabaseManager.cpp \
    ../../library-backend-a/src/services/CopyService.cpp \
    services/BorrowService.cpp \
    services/ReturnService.cpp \
    services/RenewService.cpp \
    services/ReservationService.cpp \
    services/OverdueService.cpp \
    services/PersonalService.cpp \
    services/RecommendService.cpp
