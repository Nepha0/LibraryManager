#include "database.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr auto kConnectionName = "librarymanager";
QString g_databasePath;

bool execute(QSqlQuery &query, const QString &sql, QString *error)
{
    if (query.exec(sql)) {
        return true;
    }
    if (error) {
        *error = query.lastError().text();
    }
    return false;
}

QString saltedHash(const QString &salt, const QString &password)
{
    return QString::fromLatin1(
        QCryptographicHash::hash((salt + password).toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex());
}
}

bool Database::initialize(QString *error)
{
    if (QSqlDatabase::contains(QString::fromLatin1(kConnectionName))) {
        const QSqlDatabase existing =
            QSqlDatabase::database(QString::fromLatin1(kConnectionName));
        if (existing.isOpen()) {
            return true;
        }
    }

    g_databasePath = qEnvironmentVariable("LIBRARYMANAGER_DB_PATH");
    if (g_databasePath.isEmpty()) {
        const QString dataDirectory =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        g_databasePath = dataDirectory + QStringLiteral("/librarymanager.sqlite");
    }

    const QFileInfo databaseFile(g_databasePath);
    if (!QDir().mkpath(databaseFile.absolutePath())) {
        if (error) {
            *error = QStringLiteral("无法创建数据库目录：%1")
                         .arg(databaseFile.absolutePath());
        }
        return false;
    }

    QSqlDatabase database =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                  QString::fromLatin1(kConnectionName));
    database.setDatabaseName(g_databasePath);
    if (!database.open()) {
        if (error) {
            *error = QStringLiteral("无法打开 SQLite 数据库：%1")
                         .arg(database.lastError().text());
        }
        return false;
    }

    QSqlQuery pragma(database);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))
        || !pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"))) {
        if (error) {
            *error = pragma.lastError().text();
        }
        return false;
    }

    return createSchema(error) && seedDemoData(error);
}

void Database::close()
{
    const QString connectionName = QString::fromLatin1(kConnectionName);
    if (!QSqlDatabase::contains(connectionName)) {
        return;
    }
    {
        QSqlDatabase database = QSqlDatabase::database(connectionName);
        if (database.isOpen()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
}

QSqlDatabase Database::connection()
{
    return QSqlDatabase::database(QString::fromLatin1(kConnectionName));
}

QString Database::databasePath()
{
    return g_databasePath;
}

bool Database::createSchema(QString *error)
{
    QSqlDatabase database = connection();
    QSqlQuery query(database);
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "username TEXT NOT NULL UNIQUE,"
            "display_name TEXT NOT NULL,"
            "password_hash TEXT NOT NULL,"
            "salt TEXT NOT NULL,"
            "role TEXT NOT NULL CHECK(role IN ('admin', 'librarian', 'reader')),"
            "active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS reader_types ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE,"
            "max_loans INTEGER NOT NULL CHECK(max_loans > 0),"
            "loan_days INTEGER NOT NULL CHECK(loan_days > 0))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS readers ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "reader_no TEXT NOT NULL UNIQUE,"
            "name TEXT NOT NULL,"
            "phone TEXT,"
            "type_id INTEGER NOT NULL REFERENCES reader_types(id),"
            "active INTEGER NOT NULL DEFAULT 1,"
            "created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS categories ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS books ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "isbn TEXT NOT NULL UNIQUE,"
            "title TEXT NOT NULL,"
            "author TEXT NOT NULL,"
            "publisher TEXT,"
            "publish_year INTEGER,"
            "category_id INTEGER REFERENCES categories(id),"
            "created_at TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS book_copies ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "copy_code TEXT NOT NULL UNIQUE,"
            "book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE RESTRICT,"
            "status TEXT NOT NULL DEFAULT 'available' "
            "CHECK(status IN ('available', 'borrowed', 'lost')))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS loans ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "copy_id INTEGER NOT NULL REFERENCES book_copies(id) ON DELETE RESTRICT,"
            "reader_id INTEGER NOT NULL REFERENCES readers(id) ON DELETE RESTRICT,"
            "borrowed_at TEXT NOT NULL,"
            "due_at TEXT NOT NULL,"
            "returned_at TEXT)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS reservations ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE RESTRICT,"
            "reader_id INTEGER NOT NULL REFERENCES readers(id) ON DELETE RESTRICT,"
            "created_at TEXT NOT NULL,"
            "status TEXT NOT NULL DEFAULT 'waiting' "
            "CHECK(status IN ('waiting', 'fulfilled', 'cancelled')))"),
        QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_one_active_loan_per_copy "
            "ON loans(copy_id) WHERE returned_at IS NULL"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_loans_reader "
            "ON loans(reader_id, returned_at)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_copies_book "
            "ON book_copies(book_id, status)"),
    };

    for (const QString &statement : statements) {
        if (!execute(query, statement, error)) {
            return false;
        }
    }
    return true;
}

bool Database::seedDemoData(QString *error)
{
    QSqlDatabase database = connection();
    if (!database.transaction()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }

    QSqlQuery query(database);
    const QStringList categories = {
        QStringLiteral("文学"), QStringLiteral("计算机"),
        QStringLiteral("历史"), QStringLiteral("科学")
    };
    for (const QString &name : categories) {
        query.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO categories(name) VALUES(?)"));
        query.addBindValue(name);
        if (!query.exec()) {
            database.rollback();
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }

    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO reader_types(name, max_loans, loan_days) "
        "VALUES(?, ?, ?)"));
    query.addBindValue(QStringLiteral("学生"));
    query.addBindValue(5);
    query.addBindValue(30);
    if (!query.exec()) {
        database.rollback();
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO readers("
        "reader_no, name, phone, type_id, active, created_at) "
        "SELECT ?, ?, ?, id, 1, ? FROM reader_types WHERE name = ?"));
    query.addBindValue(QStringLiteral("R001"));
    query.addBindValue(QStringLiteral("演示读者"));
    query.addBindValue(QStringLiteral("13800000000"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(QStringLiteral("学生"));
    if (!query.exec()) {
        database.rollback();
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    query.prepare(QStringLiteral("SELECT COUNT(*) FROM books"));
    if (!query.exec() || !query.next()) {
        database.rollback();
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    if (query.value(0).toInt() == 0) {
        struct DemoBook
        {
            const char *isbn;
            const char *title;
            const char *author;
            const char *category;
            int copies;
        };
        const DemoBook demoBooks[] = {
            {"9787000000001", "Qt 桌面应用实践", "实训编写组", "计算机", 2},
            {"9787000000002", "城市与记忆", "林舟", "文学", 2}
        };
        for (const DemoBook &book : demoBooks) {
            query.prepare(QStringLiteral(
                "INSERT INTO books("
                "isbn, title, author, category_id, created_at) "
                "SELECT ?, ?, ?, id, ? FROM categories WHERE name = ?"));
            query.addBindValue(QString::fromUtf8(book.isbn));
            query.addBindValue(QString::fromUtf8(book.title));
            query.addBindValue(QString::fromUtf8(book.author));
            query.addBindValue(
                QDateTime::currentDateTime().toString(Qt::ISODate));
            query.addBindValue(QString::fromUtf8(book.category));
            if (!query.exec() || query.numRowsAffected() != 1) {
                database.rollback();
                if (error) {
                    *error = query.lastError().text().isEmpty()
                                 ? QStringLiteral("初始化演示图书失败")
                                 : query.lastError().text();
                }
                return false;
            }
            const qlonglong bookId = query.lastInsertId().toLongLong();
            for (int copyIndex = 1; copyIndex <= book.copies; ++copyIndex) {
                query.prepare(QStringLiteral(
                    "INSERT INTO book_copies(copy_code, book_id, status) "
                    "VALUES(?, ?, 'available')"));
                query.addBindValue(
                    QStringLiteral("%1-C%2")
                        .arg(QString::fromUtf8(book.isbn))
                        .arg(copyIndex, 2, 10, QLatin1Char('0')));
                query.addBindValue(bookId);
                if (!query.exec()) {
                    database.rollback();
                    if (error) {
                        *error = query.lastError().text();
                    }
                    return false;
                }
            }
        }
    }

    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM users WHERE username = ?"));
    query.addBindValue(QStringLiteral("admin"));
    if (!query.exec() || !query.next()) {
        database.rollback();
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    if (query.value(0).toInt() == 0) {
        const QString salt = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString hash = saltedHash(salt, QStringLiteral("admin123"));
        query.prepare(QStringLiteral(
            "INSERT INTO users("
            "username, display_name, password_hash, salt, role, active, created_at) "
            "VALUES(?, ?, ?, ?, 'admin', 1, ?)"));
        query.addBindValue(QStringLiteral("admin"));
        query.addBindValue(QStringLiteral("系统管理员"));
        query.addBindValue(hash);
        query.addBindValue(salt);
        query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
        if (!query.exec()) {
            database.rollback();
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }

    if (!database.commit()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }
    return true;
}
