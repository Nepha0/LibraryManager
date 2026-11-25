#include "mainwindow.h"
#include "../common/database.h"
#include "../tests/service_smoke_test.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QMessageBox>
#include <QTimer>

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
    const bool uiSmoke = app.arguments().contains(QStringLiteral("--ui-smoke-test"));
    if (!(uiSmoke ? Database::initializeSmokeTest(&error) : Database::initialize(&error))) {
        qCritical().noquote() << error;
        if (!uiSmoke) {
            QMessageBox::critical(nullptr, QStringLiteral("数据库初始化失败"), error);
        }
        Database::close();
        return 1;
    }

    MainWindow window;
    window.show();
    if (uiSmoke) {
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
