#include "mainwindow.h"

#include "../books/bookservice.h"
#include "../circulation/circulationservice.h"
#include "../common/database.h"
#include "../statistics/statisticsservice.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTimer>

namespace {
bool runServiceSmokeTest()
{
    QString error;
    if (!Database::initialize(&error)) {
        qCritical().noquote() << error;
        return false;
    }

    const auto session = AuthService::login(
        QStringLiteral("admin"), QStringLiteral("admin123"), &error);
    if (!session.has_value()) {
        qCritical().noquote() << error;
        return false;
    }

    BookService books;
    const QString isbn =
        QStringLiteral("SMOKE-%1").arg(QDateTime::currentMSecsSinceEpoch());
    if (!books.addBook(*session, QStringLiteral("冒烟测试图书"),
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
    if (!circulation.borrowBook(*session, QStringLiteral("R001"),
                                copyId, &error)) {
        qCritical().noquote() << error;
        return false;
    }

    const auto activeLoans = circulation.activeLoans(&error);
    int loanId = 0;
    for (const auto &loan : activeLoans) {
        if (loan.copyCode.startsWith(isbn)) {
            loanId = loan.loanId;
            break;
        }
    }
    if (loanId == 0) {
        qCritical() << "Smoke test could not find the active loan.";
        return false;
    }
    if (!circulation.returnBook(*session, loanId, &error)) {
        qCritical().noquote() << error;
        return false;
    }

    StatisticsService statistics;
    const DashboardStats stats = statistics.dashboard(&error);
    if (!error.isEmpty() || stats.titles < 1 || stats.copies < 1) {
        qCritical().noquote()
            << (error.isEmpty() ? QStringLiteral("统计数据异常") : error);
        return false;
    }
    qInfo() << "Smoke test passed: login, add book, borrow, return, statistics.";
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

    const QString temporaryDatabase =
        QDir::tempPath() + QStringLiteral("/librarymanager-smoke.sqlite");
    if (app.arguments().contains(QStringLiteral("--smoke-test"))) {
        QFile::remove(temporaryDatabase);
        qputenv("LIBRARYMANAGER_DB_PATH", temporaryDatabase.toUtf8());
        const bool passed = runServiceSmokeTest();
        Database::close();
        QFile::remove(temporaryDatabase);
        return passed ? 0 : 2;
    }

    QString error;
    if (!Database::initialize(&error)) {
        qCritical().noquote()
            << QStringLiteral("数据库初始化失败：%1").arg(error);
        return 1;
    }

    MainWindow window;
    window.show();
    if (app.arguments().contains(QStringLiteral("--ui-smoke-test"))) {
        QTimer::singleShot(650, &window, [&window] {
            window.openDemoSession();
        });
        QTimer::singleShot(1400, &app, &QCoreApplication::quit);
    }
    const int result = app.exec();
    Database::close();
    return result;
}
