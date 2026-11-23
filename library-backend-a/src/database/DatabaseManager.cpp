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
        m_lastError = QStringLiteral("打开数据库失败: %1").arg(m_db.lastError().text());
        return false;
    }
    QSqlQuery q(m_db);
    // 外键约束必须按连接显式开启
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    return true;
}

void DatabaseManager::close()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("library_backend_a"));
}

bool DatabaseManager::initSchema(QString* errorOut)
{
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

const char* DatabaseManager::schemaSql()
{
    // 9 张核心表（阶段0 全员评审产物，A 牵头：books / categories / copies）
    // 注：SQLite 无原生布尔/枚举，状态一律 TEXT 存契约枚举串。
    return R"SCHEMA(
-- ============ B 板块牵头：用户 / 读者 ============
CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE,             -- 登录账号，唯一
    password_hash TEXT    NOT NULL,                    -- SHA-256 + 盐
    salt          TEXT    NOT NULL,
    name          TEXT    NOT NULL,
    role          TEXT    NOT NULL DEFAULT 'READER',   -- ADMIN / LIBRARIAN / READER
    status        TEXT    NOT NULL DEFAULT 'ACTIVE',   -- ACTIVE / DISABLED
    created_at    TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
);

CREATE TABLE IF NOT EXISTS readers (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    card_no       TEXT    NOT NULL UNIQUE,             -- 读者证号，唯一
    name          TEXT    NOT NULL,                    -- 姓名（无类别字段）
    phone         TEXT    DEFAULT '',
    status        TEXT    NOT NULL DEFAULT 'ACTIVE',   -- ACTIVE / DISABLED
    registered_at TEXT    NOT NULL DEFAULT (datetime('now','localtime'))
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
    isbn         TEXT NOT NULL,                      -- ISBN，唯一校验在业务层（仅 ACTIVE 范围内唯一）
    title        TEXT NOT NULL,
    author       TEXT DEFAULT '',
    publisher    TEXT DEFAULT '',
    publish_date TEXT DEFAULT '',                    -- yyyy-MM-dd
    category_id  INTEGER NOT NULL REFERENCES categories(id),
    intro        TEXT DEFAULT '',
    cover_path   TEXT DEFAULT '',
    status       TEXT NOT NULL DEFAULT 'ACTIVE',     -- ACTIVE / SHELVED_OFF（下架=逻辑删除）
    created_at   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);
CREATE INDEX IF NOT EXISTS idx_books_category  ON books(category_id);
CREATE INDEX IF NOT EXISTS idx_books_title     ON books(title);
CREATE INDEX IF NOT EXISTS idx_books_isbn      ON books(isbn);

CREATE TABLE IF NOT EXISTS copies (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    barcode       TEXT NOT NULL UNIQUE,              -- 条码，全局唯一
    book_id       INTEGER NOT NULL REFERENCES books(id),
    location      TEXT DEFAULT '',
    status        TEXT NOT NULL DEFAULT 'AVAILABLE', -- AVAILABLE/BORROWED/RESERVED/MAINTENANCE/LOST
    acquired_date TEXT DEFAULT '',                   -- 入藏日期 yyyy-MM-dd
    created_at    TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at    TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);
CREATE INDEX IF NOT EXISTS idx_copies_book ON copies(book_id);

-- ============ C 板块牵头：借阅记录 / 预约 ============
CREATE TABLE IF NOT EXISTS borrow_records (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    reader_id     INTEGER NOT NULL REFERENCES readers(id),
    book_id       INTEGER NOT NULL REFERENCES books(id),
    copy_id       INTEGER NOT NULL REFERENCES copies(id),
    borrowed_date TEXT    NOT NULL,                 -- 借出日期
    due_date      TEXT    NOT NULL,                 -- 应还日期
    returned_date TEXT    DEFAULT NULL,             -- 实际归还日期
    status        TEXT    NOT NULL DEFAULT 'BORROWED', -- BORROWED / RETURNED
    renew_count   INTEGER NOT NULL DEFAULT 0
);
-- 双保险之一：同一副本同时只允许一条有效借阅（数据库层唯一约束）
CREATE UNIQUE INDEX IF NOT EXISTS idx_active_borrow_per_copy
    ON borrow_records(copy_id) WHERE status = 'BORROWED';
CREATE INDEX IF NOT EXISTS idx_borrow_reader ON borrow_records(reader_id);
CREATE INDEX IF NOT EXISTS idx_borrow_book   ON borrow_records(book_id);

CREATE TABLE IF NOT EXISTS reservations (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id         INTEGER NOT NULL REFERENCES books(id),
    reader_id       INTEGER NOT NULL REFERENCES readers(id),
    queue_no        INTEGER NOT NULL,              -- 排队序号
    status          TEXT    NOT NULL DEFAULT 'WAITING', -- WAITING/READY_TO_PICK/FINISHED/CANCELLED/EXPIRED
    requested_at    TEXT    NOT NULL,
    pickup_deadline TEXT    DEFAULT NULL           -- 待取截止时间
);
CREATE INDEX IF NOT EXISTS idx_reservation_book ON reservations(book_id);

-- ============ B 板块牵头：系统参数 / 操作日志 ============
CREATE TABLE IF NOT EXISTS system_params (
    key        TEXT PRIMARY KEY,                   -- loan_period_days / loan_limit / max_renew_count /
                                                   -- reservation_hold_days / renew_period_days
    value      TEXT NOT NULL,
    updated_at TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

CREATE TABLE IF NOT EXISTS operation_logs (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    operator_id  INTEGER,
    operator_name TEXT NOT NULL,
    op_type      TEXT NOT NULL,                    -- BOOK_UPDATE / COPY_STATUS / ...
    target_type  TEXT NOT NULL,                    -- BOOK / COPY / READER / PARAM ...
    target_id    TEXT DEFAULT '',
    detail       TEXT DEFAULT '',
    created_at   TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 系统参数默认值（INSERT OR IGNORE 保证幂等；B 板块 ParamService 负责读写）
INSERT OR IGNORE INTO system_params(key, value) VALUES ('loan_period_days',       '30');
INSERT OR IGNORE INTO system_params(key, value) VALUES ('loan_limit',             '5');
INSERT OR IGNORE INTO system_params(key, value) VALUES ('max_renew_count',        '2');
INSERT OR IGNORE INTO system_params(key, value) VALUES ('reservation_hold_days',  '3');
INSERT OR IGNORE INTO system_params(key, value) VALUES ('renew_period_days',      '15');
)SCHEMA";
}

} // namespace lib
