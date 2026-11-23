#include "DatabaseManager.h"
#include <QSqlError>
#include <QSqlQuery>

namespace lib {

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager inst;
    return inst;
}

bool DatabaseManager::open(const QString& sqlitePath)
{
    close();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                     QStringLiteral("library_backend_a"));
    m_db.setDatabaseName(sqlitePath);
    if (!m_db.open()) {
        m_lastError = QStringLiteral("打开 SQLite 数据库失败: %1").arg(m_db.lastError().text());
        return false;
    }
    m_mySql = false;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    return true;
}

bool DatabaseManager::openMySql(const QString& odbcConnectionString)
{
    close();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QODBC"),
                                     QStringLiteral("library_backend_a"));
    m_db.setDatabaseName(odbcConnectionString);
    if (!m_db.open()) {
        m_lastError = QStringLiteral("打开 MySQL(ODBC) 失败: %1").arg(m_db.lastError().text());
        return false;
    }
    m_mySql = true;
    // 连接级字符集：防止中文读写乱码（联调第一验证项）
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SET NAMES utf8mb4"))) {
        m_lastError = QStringLiteral("设置 utf8mb4 失败: %1").arg(q.lastError().text());
        return false;
    }
    return true;
}

bool DatabaseManager::openMySqlByDsn(const QString& dsn, const QString& dbName,
                                     const QString& user, const QString& password)
{
    return openMySql(QStringLiteral("DSN=%1;DATABASE=%2;UID=%3;PWD=%4;CHARSET=utf8mb4")
                         .arg(dsn, dbName, user, password));
}

void DatabaseManager::close()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("library_backend_a"));
    m_mySql = false;
}

bool DatabaseManager::initSchema(QString* errorOut)
{
    if (m_mySql) {
        // MySQL 表结构由统一 DDL / 003 迁移脚本创建，A 侧不执行内嵌建表。
        if (errorOut) errorOut->clear();
        return true;
    }
    QSqlQuery q(m_db);
    const QString sql = QString::fromUtf8(schemaSql());
    const QStringList statements = sql.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (QString stmt : statements) {
        stmt = stmt.trimmed();
        if (stmt.isEmpty() || (stmt.startsWith(QLatin1Char('-')) && !stmt.contains(QLatin1Char('\n'))))
            continue;
        if (!q.exec(stmt)) {
            m_lastError = QStringLiteral("执行建表语句失败: %1\nSQL: %2")
                              .arg(q.lastError().text(), stmt.left(120));
            if (errorOut) *errorOut = m_lastError;
            return false;
        }
    }
    if (errorOut) errorOut->clear();
    return true;
}

QString DatabaseManager::nowExpr(const QSqlDatabase& db)
{
    // TODO 联调确认：若 B 的 DSN 未开 ANSI 兼容，MySQL 用 NOW() 无问题。
    return db.driverName().compare(QStringLiteral("QODBC"), Qt::CaseInsensitive) == 0
               ? QStringLiteral("NOW()")
               : QStringLiteral("datetime('now','localtime')");
}

const char* DatabaseManager::schemaSql()
{
    // 9 张业务表（结构与 V1.1 统一 MySQL DDL 对齐：表名/列名/小写枚举）。
    // SQLite 用 TEXT 代替 ENUM/DATE，用部分唯一索引代替 MySQL 生成列唯一约束。
    // 注意：MySQL 模式不执行本 schema（表由统一 DDL 创建）。
    return R"SCHEMA(
-- ============ B 板块牵头：用户 / 读者 ============
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE,
    display_name  TEXT    NOT NULL,
    password_hash TEXT    NOT NULL,
    salt          TEXT    NOT NULL,
    role          TEXT    NOT NULL DEFAULT 'reader',   -- admin/librarian/reader
    active        INTEGER NOT NULL DEFAULT 1,
    created_at    TEXT    NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at    TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);

CREATE TABLE IF NOT EXISTS readers (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER,
    reader_no     TEXT    NOT NULL UNIQUE,
    name          TEXT    NOT NULL,
    phone         TEXT    DEFAULT '',
    active        INTEGER NOT NULL DEFAULT 1,
    registered_at TEXT    NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at    TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);

-- ============ A 板块牵头：分类 / 图书 / 副本 ============
CREATE TABLE IF NOT EXISTS categories (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

CREATE TABLE IF NOT EXISTS books (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    isbn         TEXT NOT NULL UNIQUE,                 -- 全局唯一（含下架书）
    title        TEXT NOT NULL,
    author       TEXT DEFAULT '',
    publisher    TEXT DEFAULT '',
    publish_date TEXT DEFAULT '',                      -- yyyy-MM-dd
    category_id  INTEGER REFERENCES categories(id),
    intro        TEXT DEFAULT '',
    cover_path   TEXT DEFAULT '',
    status       TEXT NOT NULL DEFAULT 'active',       -- active/shelved_off（下架=逻辑删除）
    created_at   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);
CREATE INDEX IF NOT EXISTS idx_books_category  ON books(category_id);
CREATE INDEX IF NOT EXISTS idx_books_title     ON books(title);

CREATE TABLE IF NOT EXISTS book_copies (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    copy_code     TEXT NOT NULL UNIQUE,                -- 副本条码，全局唯一
    book_id       INTEGER NOT NULL REFERENCES books(id),
    location      TEXT NOT NULL DEFAULT '',
    status        TEXT NOT NULL DEFAULT 'available',  -- available/borrowed/reserved/maintenance/lost
    acquired_date TEXT DEFAULT '',                     -- 入藏日期 yyyy-MM-dd
    created_at    TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at    TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);
CREATE INDEX IF NOT EXISTS idx_copies_book ON book_copies(book_id, status);

-- ============ C 板块牵头：借阅记录 / 预约 ============
CREATE TABLE IF NOT EXISTS loans (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    copy_id       INTEGER NOT NULL REFERENCES book_copies(id),
    reader_id     INTEGER NOT NULL REFERENCES readers(id),
    book_id       INTEGER NOT NULL REFERENCES books(id),
    borrowed_at   TEXT NOT NULL,
    due_at        TEXT NOT NULL,
    returned_at   TEXT DEFAULT NULL,
    renew_count   INTEGER NOT NULL DEFAULT 0
);
-- 一副本同时仅一条有效借阅（SQLite 部分唯一索引 ≡ MySQL 生成列唯一约束）
CREATE UNIQUE INDEX IF NOT EXISTS uk_active_copy ON loans(copy_id) WHERE returned_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_loans_reader ON loans(reader_id, returned_at);
CREATE INDEX IF NOT EXISTS idx_loans_book   ON loans(book_id);

CREATE TABLE IF NOT EXISTS reservations (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id         INTEGER NOT NULL REFERENCES books(id),
    reader_id       INTEGER NOT NULL REFERENCES readers(id),
    queue_no        INTEGER NOT NULL DEFAULT 0,
    status          TEXT NOT NULL DEFAULT 'waiting',   -- waiting/ready_to_pick/finished/cancelled/expired
    requested_at    TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    pickup_deadline TEXT DEFAULT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS uk_active_reservation
    ON reservations(book_id, reader_id) WHERE status IN ('waiting','ready_to_pick');
CREATE INDEX IF NOT EXISTS idx_res_book ON reservations(book_id, status);

-- ============ B 板块牵头：系统参数 / 操作日志 ============
CREATE TABLE IF NOT EXISTS system_params (
    param_key   TEXT PRIMARY KEY,                      -- borrow_period_days/borrow_limit/max_renew_times/...
    param_value TEXT NOT NULL,
    description TEXT DEFAULT '',
    updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

CREATE TABLE IF NOT EXISTS operation_logs (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    operator_user_id INTEGER,
    operator_name    TEXT DEFAULT '',
    action_type      TEXT NOT NULL,
    target_type      TEXT NOT NULL,
    target_id        TEXT DEFAULT '',
    detail           TEXT DEFAULT '',
    created_at       TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 系统参数默认值（INSERT OR IGNORE 保证幂等）
INSERT OR IGNORE INTO system_params(param_key, param_value, description) VALUES
    ('borrow_period_days',      '30', '借阅期限，单位为天'),
    ('borrow_limit',            '5',  '每名读者最大借阅数量'),
    ('max_renew_times',         '2',  '每条借阅记录最大续借次数'),
    ('reservation_hold_days',   '3',  '预约待取保留期限，单位为天'),
    ('renew_period_days',       '15', '每次续借延长天数');
)SCHEMA";
}

} // namespace lib
