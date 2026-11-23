-- 003_unified_schema_v1.sql
-- 统一 MySQL 基线 V1.1 · 旧库增量迁移（对应《统一MySQL-DDL-拍板版.md》V1.1）
--
-- 用法：加入 scripts/init-mysql.ps1 的迁移列表，放在 001_backend_b.sql / 002_demo_catalog.sql 之后。
-- 特性：逐语句幂等（每条 ALTER 前查 information_schema 再 PREPARE 执行），可重复执行；
--       不使用 DELIMITER / 存储过程，每条 SQL 独占一行、行尾分号，兼容 init-mysql.ps1 的按行拆分。
-- 执行器要求：init-mysql.ps1 必须对 PREPARE / EXECUTE / DEALLOCATE / SET @ 语句直接执行，
--       不得二次包装成 PREPARE（MySQL 不支持嵌套 PREPARE）。补丁见 003执行器改造说明.md。
-- 顺序：版本表 → 建 reservations → 补列与回填 → 最后加 NOT NULL/外键 → 登记版本。
-- 安全：先在 library_manager_smoke 验证；正式库升级前必须备份。

SET NAMES utf8mb4;

-- 0. 迁移版本记录表
CREATE TABLE IF NOT EXISTS schema_migrations (
    version VARCHAR(32) NOT NULL,
    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    note VARCHAR(255) NULL,
    PRIMARY KEY (version)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 1. categories：补 description、created_at
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'categories' AND COLUMN_NAME = 'description');
SET @stmt := IF(@x = 0, 'ALTER TABLE categories ADD COLUMN description VARCHAR(255) NULL AFTER name', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'categories' AND COLUMN_NAME = 'created_at');
SET @stmt := IF(@x = 0, 'ALTER TABLE categories ADD COLUMN created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP AFTER description', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;

-- 2. books：先加 publish_date 并回填，再 DROP publish_year；补 intro/cover_path/status/updated_at 与索引
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'books' AND COLUMN_NAME = 'publish_date');
SET @stmt := IF(@x = 0, 'ALTER TABLE books ADD COLUMN publish_date DATE NULL AFTER publisher', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
-- publish_year 只在首次迁移存在；回填、检查、DROP 全部置于"列仍存在"条件内，
-- 避免第二次运行时引用已删除的列（Unknown column 'publish_year'）。
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'books' AND COLUMN_NAME = 'publish_year');
SET @stmt := IF(@x > 0, 'UPDATE books SET publish_date = STR_TO_DATE(CONCAT(publish_year, ''-01-01''), ''%Y-%m-%d'') WHERE publish_date IS NULL AND publish_year IS NOT NULL AND publish_year > 0', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @stmt := IF(@x > 0, 'SELECT COUNT(*) AS missing_publish_date FROM books WHERE publish_year IS NOT NULL AND publish_year > 0 AND publish_date IS NULL', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @stmt := IF(@x > 0, 'ALTER TABLE books DROP COLUMN publish_year', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'books' AND COLUMN_NAME = 'intro');
SET @stmt := IF(@x = 0, 'ALTER TABLE books ADD COLUMN intro TEXT NULL AFTER category_id', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'books' AND COLUMN_NAME = 'cover_path');
SET @stmt := IF(@x = 0, 'ALTER TABLE books ADD COLUMN cover_path VARCHAR(255) NULL AFTER intro', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'books' AND COLUMN_NAME = 'status');
SET @stmt := IF(@x = 0, 'ALTER TABLE books ADD COLUMN status ENUM(''active'',''shelved_off'') NOT NULL DEFAULT ''active'' AFTER cover_path', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'books' AND COLUMN_NAME = 'updated_at');
SET @stmt := IF(@x = 0, 'ALTER TABLE books ADD COLUMN updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP AFTER created_at', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'books' AND INDEX_NAME = 'idx_books_category');
SET @stmt := IF(@x = 0, 'ALTER TABLE books ADD KEY idx_books_category (category_id)', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'books' AND INDEX_NAME = 'idx_books_title');
SET @stmt := IF(@x = 0, 'ALTER TABLE books ADD KEY idx_books_title (title)', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;

-- 3. book_copies：补位置/入藏/时间戳，状态 3 态扩到 5 态
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'book_copies' AND COLUMN_NAME = 'location');
SET @stmt := IF(@x = 0, 'ALTER TABLE book_copies ADD COLUMN location VARCHAR(100) NOT NULL DEFAULT '''' AFTER book_id', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @stmt := 'ALTER TABLE book_copies MODIFY COLUMN status ENUM(''available'',''borrowed'',''reserved'',''maintenance'',''lost'') NOT NULL DEFAULT ''available''';
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'book_copies' AND COLUMN_NAME = 'acquired_date');
SET @stmt := IF(@x = 0, 'ALTER TABLE book_copies ADD COLUMN acquired_date DATE NULL AFTER status', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'book_copies' AND COLUMN_NAME = 'created_at');
SET @stmt := IF(@x = 0, 'ALTER TABLE book_copies ADD COLUMN created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP AFTER acquired_date', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'book_copies' AND COLUMN_NAME = 'updated_at');
SET @stmt := IF(@x = 0, 'ALTER TABLE book_copies ADD COLUMN updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP AFTER created_at', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;

-- 4. loans：book_id 先 NULL 加入 → 回填 → 无空值才 NOT NULL → 再加索引与外键；日期列保留 _at 改 DATE
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'loans' AND COLUMN_NAME = 'book_id');
SET @stmt := IF(@x = 0, 'ALTER TABLE loans ADD COLUMN book_id BIGINT UNSIGNED NULL AFTER reader_id', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
UPDATE loans l JOIN book_copies bc ON bc.id = l.copy_id SET l.book_id = bc.book_id WHERE l.book_id IS NULL;
SELECT COUNT(*) AS missing_book_id FROM loans WHERE book_id IS NULL;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'loans' AND COLUMN_NAME = 'book_id' AND IS_NULLABLE = 'YES');
SET @stmt := IF(@x > 0, 'ALTER TABLE loans MODIFY COLUMN book_id BIGINT UNSIGNED NOT NULL', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'loans' AND COLUMN_NAME = 'renew_count');
SET @stmt := IF(@x = 0, 'ALTER TABLE loans ADD COLUMN renew_count INT NOT NULL DEFAULT 0 AFTER book_id', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @stmt := 'ALTER TABLE loans MODIFY COLUMN borrowed_at DATE NOT NULL, MODIFY COLUMN due_at DATE NOT NULL, MODIFY COLUMN returned_at DATE NULL';
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'loans' AND INDEX_NAME = 'idx_loans_book');
SET @stmt := IF(@x = 0, 'ALTER TABLE loans ADD KEY idx_loans_book (book_id)', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'loans' AND CONSTRAINT_NAME = 'fk_loans_book');
SET @stmt := IF(@x = 0, 'ALTER TABLE loans ADD CONSTRAINT fk_loans_book FOREIGN KEY (book_id) REFERENCES books(id)', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;

-- 5. reservations：旧库无此表，先完整建表（IF NOT EXISTS 幂等），再对可能存在的旧表补列/索引
CREATE TABLE IF NOT EXISTS reservations (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    book_id BIGINT UNSIGNED NOT NULL,
    reader_id BIGINT UNSIGNED NOT NULL,
    queue_no INT NOT NULL DEFAULT 0,
    status ENUM('waiting','ready_to_pick','finished','cancelled','expired') NOT NULL DEFAULT 'waiting',
    requested_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    pickup_deadline DATETIME NULL,
    active_rid BIGINT UNSIGNED GENERATED ALWAYS AS
        (CASE WHEN status IN ('waiting','ready_to_pick') THEN reader_id ELSE NULL END) STORED,
    PRIMARY KEY (id),
    UNIQUE KEY uk_active_reservation (book_id, active_rid),
    KEY idx_res_book (book_id, status),
    CONSTRAINT fk_res_book FOREIGN KEY (book_id) REFERENCES books(id),
    CONSTRAINT fk_res_reader FOREIGN KEY (reader_id) REFERENCES readers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
SET @stmt := 'ALTER TABLE reservations MODIFY COLUMN status ENUM(''waiting'',''ready_to_pick'',''finished'',''cancelled'',''expired'') NOT NULL DEFAULT ''waiting''';
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'reservations' AND COLUMN_NAME = 'queue_no');
SET @stmt := IF(@x = 0, 'ALTER TABLE reservations ADD COLUMN queue_no INT NOT NULL DEFAULT 0 AFTER reader_id', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'reservations' AND COLUMN_NAME = 'pickup_deadline');
SET @stmt := IF(@x = 0, 'ALTER TABLE reservations ADD COLUMN pickup_deadline DATETIME NULL AFTER requested_at', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'reservations' AND COLUMN_NAME = 'active_rid');
SET @stmt := IF(@x = 0, 'ALTER TABLE reservations ADD COLUMN active_rid BIGINT UNSIGNED GENERATED ALWAYS AS (CASE WHEN status IN (''waiting'',''ready_to_pick'') THEN reader_id ELSE NULL END) STORED AFTER pickup_deadline', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'reservations' AND INDEX_NAME = 'uk_active_reservation');
SET @stmt := IF(@x = 0, 'ALTER TABLE reservations ADD UNIQUE KEY uk_active_reservation (book_id, active_rid)', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
SET @x := (SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'reservations' AND INDEX_NAME = 'idx_res_book');
SET @stmt := IF(@x = 0, 'ALTER TABLE reservations ADD KEY idx_res_book (book_id, status)', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;

-- 6. system_params：仅在键缺失时补默认值 2；已有值可能已被管理员调整，绝不覆盖。
INSERT IGNORE INTO system_params (param_key, param_value, description)
VALUES ('max_renew_times', '2', '每条借阅记录最大续借次数');

-- 7. operation_logs：补操作人姓名快照列并回填旧数据
SET @x := (SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'operation_logs' AND COLUMN_NAME = 'operator_name');
SET @stmt := IF(@x = 0, 'ALTER TABLE operation_logs ADD COLUMN operator_name VARCHAR(100) NULL AFTER operator_user_id', 'SELECT 1');
PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;
UPDATE operation_logs ol LEFT JOIN users u ON u.id = ol.operator_user_id SET ol.operator_name = COALESCE(u.display_name, u.username, '系统') WHERE ol.operator_name IS NULL;

-- 8. 登记迁移版本
INSERT IGNORE INTO schema_migrations (version, note) VALUES ('v1.1_unified', '统一 MySQL 基线 V1.1：9 表、_at 日期列、5 态副本/预约、book_id 与 operator_name 回填');
