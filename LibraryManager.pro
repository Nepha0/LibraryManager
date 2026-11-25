QT += core gui widgets sql
CONFIG += c++17
TEMPLATE = app
TARGET = LibraryManager
INCLUDEPATH += $$PWD
INCLUDEPATH += $$PWD/library-backend-a/src
include(library-backend-b/backend-b.pri)
SOURCES += app/main.cpp \
           app/mainwindow.cpp \
           app/mainwindow_books.cpp \
           app/mainwindow_readers.cpp \
           app/mainwindow_circulation.cpp \
           app/mainwindow_reservations.cpp \
           app/mainwindow_reports.cpp \
           app/windowwidgets.cpp \
           tests/service_smoke_test.cpp \
           library-backend-a/src/services/BookService.cpp \
           library-backend-a/src/services/CopyService.cpp \
           library-backend-a/src/database/DatabaseManager.cpp \
           library-backend-c/src/services/BorrowService.cpp \
           library-backend-c/src/services/ReturnService.cpp \
           library-backend-c/src/services/RenewService.cpp \
           library-backend-c/src/services/ReservationService.cpp \
           common/database.cpp \
           books/demobookservice.cpp \
           circulation/circulationservice.cpp \
           statistics/statisticsservice.cpp
HEADERS += app/mainwindow.h \
           app/windowwidgets.h \
           tests/service_smoke_test.h \
           app/backendintegration.h \
           common/database.h \
           books/bookservice.h \
           circulation/circulationservice.h \
           statistics/statisticsservice.h
RESOURCES += resources/resources.qrc
