-- ============================================================
-- 图书馆管理系统 · 9 张业务表 DDL（SQLite 版，V1.1 适配）
-- 结构与 docs/统一MySQL-DDL-V1.1-干净版.sql 对齐（表名/列名/枚举小写）
-- 单一事实来源：src/database/DatabaseManager.cpp 的 schemaSql()
-- 本文件由其提取，仅用于评审阅读；变更请改 C++ 源后重新同步
-- MySQL 生产库不使用本文件（由统一 DDL / 003 迁移脚本建表）
-- ============================================================

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
