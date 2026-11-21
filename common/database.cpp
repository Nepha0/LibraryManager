#include "database.h"

#include <QSqlError>

namespace {
constexpr auto kConnectionName = "librarymanager";
constexpr auto kDefaultDsn = "LibraryManagerDSN";
}

bool Database::initialize(QString *error)
{
    const QString connectionName = QString::fromLatin1(kConnectionName);

    QSqlDatabase database;
    if (QSqlDatabase::contains(connectionName)) {
        database = QSqlDatabase::database(connectionName, false);
        if (database.isOpen()) {
            return true;
        }
    } else {
        database = QSqlDatabase::addDatabase(
            QStringLiteral("QODBC"), connectionName);
    }

    const QString username =
        qEnvironmentVariable("LIBRARYMANAGER_DB_USER");
    const QString password =
        qEnvironmentVariable("LIBRARYMANAGER_DB_PASSWORD");
    const QString dsn =
        qEnvironmentVariable("LIBRARYMANAGER_DSN",
                             QString::fromLatin1(kDefaultDsn));

    if (username.isEmpty() || password.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "未配置数据库账号。请设置 "
                "LIBRARYMANAGER_DB_USER 和 "
                "LIBRARYMANAGER_DB_PASSWORD 环境变量");
        }
        return false;
    }

    database.setDatabaseName(dsn);
    database.setUserName(username);
    database.setPassword(password);
    database.setConnectOptions(
        QStringLiteral("SQL_ATTR_LOGIN_TIMEOUT=5"));

    if (!database.open()) {
        if (error) {
            *error = QStringLiteral("无法连接 MySQL 数据库：%1")
                         .arg(database.lastError().text());
        }
        return false;
    }

    return true;
}

void Database::close()
{
    const QString connectionName = QString::fromLatin1(kConnectionName);
    if (!QSqlDatabase::contains(connectionName)) {
        return;
    }

    {
        QSqlDatabase database =
            QSqlDatabase::database(connectionName, false);
        if (database.isOpen()) {
            database.close();
        }
    }

    QSqlDatabase::removeDatabase(connectionName);
}

QSqlDatabase Database::connection()
{
    return QSqlDatabase::database(
        QString::fromLatin1(kConnectionName));
}