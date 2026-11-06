PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    salt TEXT NOT NULL,
    role TEXT NOT NULL CHECK(role IN ('admin', 'librarian', 'reader')),
    active INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS reader_types (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    max_loans INTEGER NOT NULL CHECK(max_loans > 0),
    loan_days INTEGER NOT NULL CHECK(loan_days > 0)
);

CREATE TABLE IF NOT EXISTS readers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    reader_no TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    phone TEXT,
    type_id INTEGER NOT NULL REFERENCES reader_types(id),
    active INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS books (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    isbn TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL,
    author TEXT NOT NULL,
    publisher TEXT,
    publish_year INTEGER,
    category_id INTEGER REFERENCES categories(id),
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS book_copies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    copy_code TEXT NOT NULL UNIQUE,
    book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE RESTRICT,
    status TEXT NOT NULL DEFAULT 'available'
        CHECK(status IN ('available', 'borrowed', 'lost'))
);

CREATE TABLE IF NOT EXISTS loans (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    copy_id INTEGER NOT NULL REFERENCES book_copies(id) ON DELETE RESTRICT,
    reader_id INTEGER NOT NULL REFERENCES readers(id) ON DELETE RESTRICT,
    borrowed_at TEXT NOT NULL,
    due_at TEXT NOT NULL,
    returned_at TEXT
);

CREATE TABLE IF NOT EXISTS reservations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE RESTRICT,
    reader_id INTEGER NOT NULL REFERENCES readers(id) ON DELETE RESTRICT,
    created_at TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'waiting'
        CHECK(status IN ('waiting', 'fulfilled', 'cancelled'))
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_one_active_loan_per_copy
    ON loans(copy_id) WHERE returned_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_loans_reader
    ON loans(reader_id, returned_at);
CREATE INDEX IF NOT EXISTS idx_copies_book
    ON book_copies(book_id, status);
