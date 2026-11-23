-- ============================================================
-- 图书馆管理系统 · 9 张核心表 DDL（阶段0 契约产物，供全组评审）
-- 单一事实来源：src/database/DatabaseManager.cpp 内嵌 SCHEMA_SQL
-- 本文件由其生成，仅用于评审阅读；变更请改 C++ 源后重新同步
-- A 牵头：categories / books / copies
-- B 牵头：users / readers / system_params / operation_logs
-- C 牵头：borrow_records / reservations
-- ============================================================

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
