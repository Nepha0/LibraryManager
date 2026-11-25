# 统一 MySQL DDL · V1.1

> 全组唯一表结构事实来源（9 张业务表 + 1 张迁移版本表）。给 C / F 建 SQL 与对字段用；**空库一次初始化用本文件，旧库升级走 003 版本迁移**。  
> 统一口径：InnoDB / utf8mb4_unicode_ci / 主键 BIGINT UNSIGNED / 枚举小写；业务日期 DATE、记录时间 DATETIME；逾期判断 `returned_at IS NULL AND due_at < CURRENT_DATE`。



```sql
SET NAMES utf8mb4;

-- 1. 用户账号表
CREATE TABLE IF NOT EXISTS users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username VARCHAR(50) NOT NULL,
    display_name VARCHAR(100) NOT NULL,
    password_hash CHAR(64) NOT NULL,
    salt VARCHAR(64) NOT NULL,
    role ENUM('admin','librarian','reader') NOT NULL DEFAULT 'reader',
    active TINYINT(1) NOT NULL DEFAULT 1,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_users_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 2. 读者表
CREATE TABLE IF NOT EXISTS readers (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    user_id BIGINT UNSIGNED NULL,
    reader_no VARCHAR(30) NOT NULL,
    name VARCHAR(100) NOT NULL,
    phone VARCHAR(30) NULL,
    active TINYINT(1) NOT NULL DEFAULT 1,
    registered_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_readers_reader_no (reader_no),
    UNIQUE KEY uk_readers_user_id (user_id),
    CONSTRAINT fk_readers_user FOREIGN KEY (user_id) REFERENCES users(id)
        ON UPDATE CASCADE ON DELETE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 3. 分类表
CREATE TABLE IF NOT EXISTS categories (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    name VARCHAR(100) NOT NULL,
    description VARCHAR(255) NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_categories_name (name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 4. 图书表
CREATE TABLE IF NOT EXISTS books (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    isbn VARCHAR(64) NOT NULL,                        -- 全局唯一（含下架书）
    title VARCHAR(255) NOT NULL,
    author VARCHAR(255) NOT NULL DEFAULT '',
    publisher VARCHAR(255) NULL,
    publish_date DATE NULL,                           -- 年份筛选用 YEAR(publish_date)
    category_id BIGINT UNSIGNED NULL,                 -- 业务层校验必填，外键留空可容错
    intro TEXT NULL,
    cover_path VARCHAR(255) NULL,
    status ENUM('active','shelved_off') NOT NULL DEFAULT 'active',  -- 下架=逻辑删除
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_books_isbn (isbn),
    KEY idx_books_category (category_id),
    KEY idx_books_title (title),
    CONSTRAINT fk_books_category FOREIGN KEY (category_id) REFERENCES categories(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 5. 馆藏副本表（状态机唯一合法转换源在 A 的 CopyService）
CREATE TABLE IF NOT EXISTS book_copies (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    copy_code VARCHAR(96) NOT NULL,
    book_id BIGINT UNSIGNED NOT NULL,
    location VARCHAR(100) NOT NULL DEFAULT '',
    status ENUM('available','borrowed','reserved','maintenance','lost')
        NOT NULL DEFAULT 'available',
    acquired_date DATE NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_copies_code (copy_code),
    KEY idx_copies_book (book_id, status),
    CONSTRAINT fk_copies_book FOREIGN KEY (book_id) REFERENCES books(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 6. 借阅记录表（不设 status 列：在借=returned_at IS NULL，逾期=在借且 due_at<今天）
CREATE TABLE IF NOT EXISTS loans (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    copy_id BIGINT UNSIGNED NOT NULL,
    reader_id BIGINT UNSIGNED NOT NULL,
    book_id BIGINT UNSIGNED NOT NULL,                 -- 冗余列，免三表联查；C 写入时必须与 copy_id 对应
    borrowed_at DATE NOT NULL,
    due_at DATE NOT NULL,
    returned_at DATE NULL,
    renew_count INT NOT NULL DEFAULT 0,
    active_copy_id BIGINT UNSIGNED GENERATED ALWAYS AS
        (CASE WHEN returned_at IS NULL THEN copy_id ELSE NULL END) STORED,
    PRIMARY KEY (id),
    UNIQUE KEY uk_active_copy (active_copy_id),       -- 一副本同时仅一条有效借阅
    KEY idx_loans_reader (reader_id, returned_at),
    KEY idx_loans_book (book_id),
    CONSTRAINT fk_loans_copy   FOREIGN KEY (copy_id)   REFERENCES book_copies(id),
    CONSTRAINT fk_loans_reader FOREIGN KEY (reader_id) REFERENCES readers(id),
    CONSTRAINT fk_loans_book   FOREIGN KEY (book_id)   REFERENCES books(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 7. 预约表
CREATE TABLE IF NOT EXISTS reservations (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    book_id BIGINT UNSIGNED NOT NULL,
    reader_id BIGINT UNSIGNED NOT NULL,
    queue_no INT NOT NULL DEFAULT 0,                  -- 同书内排队序号（按申请时间维护）
    status ENUM('waiting','ready_to_pick','finished','cancelled','expired')
        NOT NULL DEFAULT 'waiting',
    requested_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    pickup_deadline DATETIME NULL,                    -- 转 ready_to_pick 时写入；超时→expired
    active_rid BIGINT UNSIGNED GENERATED ALWAYS AS
        (CASE WHEN status IN ('waiting','ready_to_pick') THEN reader_id ELSE NULL END) STORED,
    PRIMARY KEY (id),
    UNIQUE KEY uk_active_reservation (book_id, active_rid),  -- 同一读者同书仅一条有效预约
    KEY idx_res_book (book_id, status),
    CONSTRAINT fk_res_book   FOREIGN KEY (book_id)   REFERENCES books(id),
    CONSTRAINT fk_res_reader FOREIGN KEY (reader_id) REFERENCES readers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 8. 系统参数表（键名沿用 B；max_renew_times 默认 1→2）
CREATE TABLE IF NOT EXISTS system_params (
    param_key VARCHAR(50) NOT NULL,
    param_value VARCHAR(255) NOT NULL,
    description VARCHAR(255) NULL,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (param_key)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT IGNORE INTO system_params (param_key, param_value, description) VALUES
    ('borrow_period_days',      '30', '借阅期限，单位为天'),
    ('borrow_limit',            '5',  '每名读者最大借阅数量'),
    ('max_renew_times',         '2',  '每条借阅记录最大续借次数'),
    ('reservation_hold_days',   '3',  '预约待取保留期限，单位为天'),
    ('renew_period_days',       '15', '每次续借延长天数');

-- 9. 操作日志表（operator_name 为操作发生时姓名快照，查询优先读快照）
CREATE TABLE IF NOT EXISTS operation_logs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    operator_user_id BIGINT UNSIGNED NULL,
    operator_name VARCHAR(100) NULL,
    action_type VARCHAR(50) NOT NULL,
    target_type VARCHAR(50) NOT NULL,
    target_id VARCHAR(64) NULL,
    detail TEXT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_logs_operator (operator_user_id),
    KEY idx_logs_action_time (action_type, created_at),
    CONSTRAINT fk_logs_operator FOREIGN KEY (operator_user_id) REFERENCES users(id)
        ON UPDATE CASCADE ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 10. 迁移版本表（运维用，不计入 9 张业务表）
CREATE TABLE IF NOT EXISTS schema_migrations (
    version VARCHAR(32) NOT NULL,
    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    note VARCHAR(255) NULL,
    PRIMARY KEY (version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
INSERT IGNORE INTO schema_migrations (version, note)
VALUES ('v1.1_unified', '统一 MySQL 基线 V1.1（空库初始化；旧库请执行 003_unified_schema_v1.sql）');

-- 可选演示账号：与 B 的 001_backend_b.sql 一致（admin / admin123），不需要可删除本段
INSERT IGNORE INTO users (username, display_name, password_hash, salt, role, active)
VALUES ('admin', '系统管理员', SHA2(CONCAT('library-demo-salt', 'admin123'), 256), 'library-demo-salt', 'admin', 1);
```
