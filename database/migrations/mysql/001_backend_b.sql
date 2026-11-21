\USE library_manager;

-- 1. 用户账号表
CREATE TABLE IF NOT EXISTS users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username VARCHAR(50) NOT NULL,
    display_name VARCHAR(100) NOT NULL,
    password_hash CHAR(64) NOT NULL,
    salt VARCHAR(64) NOT NULL,
    role ENUM('admin', 'librarian', 'reader')
        NOT NULL DEFAULT 'reader',
    active TINYINT(1) NOT NULL DEFAULT 1,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uk_users_username (username)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

-- 2. 读者资料表
-- 新需求明确规定：不设置读者类别
CREATE TABLE IF NOT EXISTS readers (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    user_id BIGINT UNSIGNED NULL,
    reader_no VARCHAR(30) NOT NULL,
    name VARCHAR(100) NOT NULL,
    phone VARCHAR(30) NULL,
    active TINYINT(1) NOT NULL DEFAULT 1,
    registered_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    UNIQUE KEY uk_readers_reader_no (reader_no),
    UNIQUE KEY uk_readers_user_id (user_id),

    CONSTRAINT fk_readers_user
        FOREIGN KEY (user_id)
        REFERENCES users(id)
        ON UPDATE CASCADE
        ON DELETE RESTRICT
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

-- 3. 系统参数表
CREATE TABLE IF NOT EXISTS system_params (
    param_key VARCHAR(50) NOT NULL,
    param_value VARCHAR(255) NOT NULL,
    description VARCHAR(255) NULL,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
        ON UPDATE CURRENT_TIMESTAMP,

    PRIMARY KEY (param_key)
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

-- 4. 操作日志表
CREATE TABLE IF NOT EXISTS operation_logs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    operator_user_id BIGINT UNSIGNED NULL,
    action_type VARCHAR(50) NOT NULL,
    target_type VARCHAR(50) NOT NULL,
    target_id VARCHAR(64) NULL,
    detail TEXT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,

    PRIMARY KEY (id),
    KEY idx_operation_logs_operator (operator_user_id),
    KEY idx_operation_logs_action_time (action_type, created_at),
    KEY idx_operation_logs_created_at (created_at),

    CONSTRAINT fk_operation_logs_operator
        FOREIGN KEY (operator_user_id)
        REFERENCES users(id)
        ON UPDATE CASCADE
        ON DELETE SET NULL
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;

-- 5. 系统默认参数
INSERT IGNORE INTO system_params
    (param_key, param_value, description)
VALUES
    ('borrow_period_days', '30', '借阅期限，单位为天'),
    ('borrow_limit', '5', '每名读者最大借阅数量'),
    ('max_renew_times', '1', '每条借阅记录最大续借次数'),
    ('reservation_hold_days', '3', '预约待取保留期限，单位为天'),
    ('renew_period_days', '15', '每次续借延长天数');

-- 6. 演示管理员：admin / admin123
INSERT IGNORE INTO users (
    username,
    display_name,
    password_hash,
    salt,
    role,
    active
)
VALUES (
    'admin',
    '系统管理员',
    SHA2(CONCAT('library-demo-salt', 'admin123'), 256),
    'library-demo-salt',
    'admin',
    1
);