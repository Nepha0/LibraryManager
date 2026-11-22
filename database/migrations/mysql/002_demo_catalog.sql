-- Run after 001_backend_b.sql in the same MySQL database.
CREATE TABLE IF NOT EXISTS categories (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL UNIQUE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS books (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    isbn VARCHAR(64) NOT NULL UNIQUE,
    title VARCHAR(255) NOT NULL,
    author VARCHAR(255) NOT NULL,
    publisher VARCHAR(255) NULL,
    publish_year INT NULL,
    category_id BIGINT UNSIGNED NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (category_id) REFERENCES categories(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS book_copies (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    copy_code VARCHAR(96) NOT NULL UNIQUE,
    book_id BIGINT UNSIGNED NOT NULL,
    status ENUM('available', 'borrowed', 'lost') NOT NULL DEFAULT 'available',
    KEY idx_copies_book (book_id, status),
    FOREIGN KEY (book_id) REFERENCES books(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS loans (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    copy_id BIGINT UNSIGNED NOT NULL,
    reader_id BIGINT UNSIGNED NOT NULL,
    borrowed_at DATETIME NOT NULL,
    due_at DATETIME NOT NULL,
    returned_at DATETIME NULL,
    active_copy_id BIGINT UNSIGNED GENERATED ALWAYS AS
        (CASE WHEN returned_at IS NULL THEN copy_id ELSE NULL END) STORED,
    UNIQUE KEY uk_active_copy (active_copy_id),
    KEY idx_loans_reader (reader_id, returned_at),
    FOREIGN KEY (copy_id) REFERENCES book_copies(id),
    FOREIGN KEY (reader_id) REFERENCES readers(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT IGNORE INTO categories (name) VALUES ('文学'), ('计算机'), ('历史'), ('科学');
INSERT IGNORE INTO readers (reader_no, name) VALUES ('R001', '演示读者');

INSERT IGNORE INTO books (isbn, title, author, category_id)
SELECT '9787000000001', 'Qt 桌面应用实践', '实训编写组', id FROM categories WHERE name = '计算机';
INSERT IGNORE INTO books (isbn, title, author, category_id)
SELECT '9787000000002', '城市与记忆', '林舟', id FROM categories WHERE name = '文学';

INSERT IGNORE INTO book_copies (copy_code, book_id)
SELECT CONCAT(b.isbn, '-C', c.suffix), b.id
FROM books b CROSS JOIN (SELECT '01' AS suffix UNION ALL SELECT '02') c
WHERE b.isbn IN ('9787000000001', '9787000000002');
