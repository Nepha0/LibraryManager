#include "mainwindow.h"

#include "../books/bookservice.h"
#include "../circulation/circulationservice.h"
#include "../common/database.h"
#include "../statistics/statisticsservice.h"
#include "../readers/readerservice.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QMessageBox>
#include <QTimer>

namespace {
bool runServiceSmokeTest()
{
    QString error;
    if (!Database::initializeSmokeTest(&error)) {
        qCritical().noquote() << error;
        return false;
    }

    const auto session = AuthService::login(
        QStringLiteral("admin"), QStringLiteral("admin123"), &error);
    if (!session.has_value()) {
        qCritical().noquote() << error;
        return false;
    }

    const QString suffix = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString readerNo = QStringLiteral("SMOKE-%1").arg(suffix);
    const QString readerName = QStringLiteral("冒烟测试读者");
    ReaderService readers;
    if (!readers.addReader(*session, readerNo, readerName, QString(), &error)) {
        qCritical().noquote() << error;
        return false;
    }

    StatisticsService statistics;
    const DashboardStats before = statistics.dashboard(&error);
    if (!error.isEmpty()) {
        qCritical().noquote() << error;
        return false;
    }

    BookService books;
    const QString isbn =
        QStringLiteral("SMOKE-%1").arg(suffix);
    const QString title = QStringLiteral("冒烟测试图书");
    if (!books.addBook(*session, title,
                       QStringLiteral("测试作者"), isbn,
                       QStringLiteral("计算机"), 1, &error)) {
        qCritical().noquote() << error;
        return false;
    }

    const auto copies = books.availableCopies(&error);
    int copyId = 0;
    for (const auto &copy : copies) {
        if (copy.code.startsWith(isbn)) {
            copyId = copy.id;
            break;
        }
    }
    if (copyId == 0) {
        qCritical() << "Smoke test could not find the new copy.";
        return false;
    }

    CirculationService circulation;
    if (!circulation.borrowBook(*session, readerNo,
                                copyId, &error)) {
        qCritical().noquote() << error;
        return false;
    }

    const auto activeLoans = circulation.activeLoans(&error);
    int loanId = 0;
    for (const auto &loan : activeLoans) {
        if (loan.copyCode.startsWith(isbn) && loan.readerName == readerName
            && loan.title == title && loan.borrowedAt.isValid() && loan.dueAt.isValid()) {
            loanId = loan.loanId;
            break;
        }
    }
    if (loanId == 0) {
        qCritical() << "Smoke test could not find the active loan.";
        return false;
    }
    const DashboardStats during = statistics.dashboard(&error);
    if (!error.isEmpty() || during.activeLoans != before.activeLoans + 1
        || during.availableCopies != before.availableCopies) {
        qCritical().noquote() << "Borrow did not update statistics as expected." << error;
        return false;
    }
    if (circulation.borrowBook(*session, readerNo, copyId, &error)
        || error != QStringLiteral("该实体册当前不可借")) {
        qCritical().noquote() << "A second borrow was not rejected as expected." << error;
        return false;
    }
    error.clear();
    if (!circulation.returnBook(*session, loanId, &error)) {
        qCritical().noquote() << error;
        return false;
    }

    const auto history = statistics.loanHistory(1, &error);
    if (history.isEmpty() || history.front().readerNo != readerNo
        || !history.front().returnedAt.isValid()) {
        qCritical().noquote() << "Returned loan is missing from history." << error;
        return false;
    }
    const DashboardStats stats = statistics.dashboard(&error);
    if (!error.isEmpty() || stats.titles != before.titles + 1
        || stats.availableCopies != before.availableCopies + 1
        || stats.activeLoans != before.activeLoans) {
        qCritical().noquote()
            << (error.isEmpty() ? QStringLiteral("统计数据异常") : error);
        return false;
    }
    qInfo() << "Smoke test passed: login, register reader, add book, borrow, reject duplicate, return, statistics.";
    return true;
}
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("LibraryTeam"));
    app.setApplicationName(QStringLiteral("LibraryManager"));

    QFile styleFile(QStringLiteral(":/styles/app.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        const bool passed = runServiceSmokeTest();
        Database::close();
        return passed ? 0 : 2;
    }

    QString error;
    if (!Database::initialize(&error)) {
        qCritical().noquote() << error;
        if (!app.arguments().contains(QStringLiteral("--ui-smoke-test"))) {
            QMessageBox::critical(nullptr, QStringLiteral("数据库初始化失败"), error);
        }
        Database::close();
        return 1;
    }

    MainWindow window;
    window.show();
    if (app.arguments().contains(QStringLiteral("--ui-smoke-test"))) {
        QTimer::singleShot(0, &window, [&app, &window] {
            if (!window.openDemoSession()) {
                qCritical() << "UI smoke test could not open the dashboard.";
                app.exit(3);
                return;
            }
            qInfo() << "UI smoke test passed: login and dashboard.";
            QTimer::singleShot(200, &app, &QCoreApplication::quit);
        });
    }
    const int result = app.exec();
    Database::close();
    return result;
}
