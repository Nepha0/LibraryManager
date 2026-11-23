#include "database.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
constexpr auto kConnectionName = "librarymanager";
constexpr auto kDefaultDsn = "LibraryManagerDSN";
}

bool Database::initialize(QString *error)
{
    return open(false, error);
}

bool Database::initializeSmokeTest(QString *error)
{
    return open(true, error);
}

bool Database::open(bool smokeTest, QString *error)
{
    const QString connectionName = QString::fromLatin1(kConnectionName);

    QSqlDatabase database;
    if (QSqlDatabase::contains(connectionName)) {
        database = QSqlDatabase::database(connectionName, false);
        if (database.isOpen()) {
            database.close();
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

    if (username.isEmpty() && !password.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "设置 LIBRARYMANAGER_DB_PASSWORD 时须同时设置 "
                "LIBRARYMANAGER_DB_USER");
        }
        return false;
    }

    static const QRegularExpression dsnPattern(QStringLiteral(R"(^[\w .-]+$)"),
                                               QRegularExpression::UseUnicodePropertiesOption);
    if (!dsnPattern.match(dsn).hasMatch()) {
        if (error) {
            *error = QStringLiteral("LIBRARYMANAGER_DSN 必须填写 ODBC 数据源名称");
        }
        return false;
    }

    database.setDatabaseName(dsn + QStringLiteral(";CHARSET=gb18030"));
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

    const QString expectedSchema = smokeTest ? QStringLiteral("library_manager_smoke")
                                             : QStringLiteral("library_manager");
    QString validationError;
    {
        QSqlQuery query(database);
        if ((smokeTest && !query.exec(QStringLiteral("USE library_manager_smoke")))
            || !query.exec(QStringLiteral("SELECT DATABASE()")) || !query.next()
            || query.value(0).toString() != expectedSchema) {
            validationError = QStringLiteral("未连接到 %1。请检查 ODBC 数据源设置和建库步骤。")
                                  .arg(expectedSchema);
        } else if (!query.exec(QStringLiteral(
                       "SELECT COUNT(*) FROM information_schema.tables "
                       "WHERE table_schema = DATABASE() AND table_name IN ("
                       "'users','readers','system_params','operation_logs',"
                       "'categories','books','book_copies','loans','reservations')"))
                   || !query.next() || query.value(0).toInt() != 9) {
            validationError = QStringLiteral("%1 尚未初始化。请运行 scripts/init-mysql.ps1%2。")
                                  .arg(expectedSchema, smokeTest ? QStringLiteral(" -Smoke") : QString());
        }
    }
    if (!validationError.isEmpty()) {
        database.close();
        if (error) {
            *error = validationError;
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
