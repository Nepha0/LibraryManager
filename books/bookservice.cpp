#include "bookservice.h"

#include "../common/database.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

bool BookService::addBook(const UserSession &session,
                          const QString &title,
                          const QString &author,
                          const QString &isbn,
                          const QString &category,
                          int copyCount,
                          QString *error)
{
    if (!session.isAdmin()) {
        if (error) {
            *error = QStringLiteral("只有管理员可以新增馆藏");
        }
        return false;
    }
    if (title.trimmed().isEmpty() || author.trimmed().isEmpty()
        || isbn.trimmed().isEmpty() || category.trimmed().isEmpty()
        || copyCount < 1) {
        if (error) {
            *error = QStringLiteral("请填写完整的图书信息");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();
    if (!database.transaction()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT id FROM categories WHERE name = ?"));
    query.addBindValue(category);
    if (!query.exec() || !query.next()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("所选分类不存在");
        }
        return false;
    }
    const int categoryId = query.value(0).toInt();

    query.prepare(QStringLiteral(
        "INSERT INTO books("
        "isbn, title, author, category_id, created_at) VALUES(?, ?, ?, ?, ?)"));
    query.addBindValue(isbn.trimmed());
    query.addBindValue(title.trimmed());
    query.addBindValue(author.trimmed());
    query.addBindValue(categoryId);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!query.exec()) {
        database.rollback();
        if (error) {
            *error = query.lastError().nativeErrorCode() == QStringLiteral("19")
                         ? QStringLiteral("该 ISBN 已存在")
                         : query.lastError().text();
        }
        return false;
    }
    const qlonglong bookId = query.lastInsertId().toLongLong();

    for (int index = 1; index <= copyCount; ++index) {
        query.prepare(QStringLiteral(
            "INSERT INTO book_copies(copy_code, book_id, status) "
            "VALUES(?, ?, 'available')"));
        query.addBindValue(QStringLiteral("%1-C%2")
                               .arg(isbn.trimmed())
                               .arg(index, 2, 10, QLatin1Char('0')));
        query.addBindValue(bookId);
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

QVector<BookInfo> BookService::books(QString *error) const
{
    QVector<BookInfo> result;
    QSqlQuery query(Database::connection());
    const QString sql = QStringLiteral(
        "SELECT b.id, b.isbn, b.title, b.author, "
        "COALESCE(c.name, '未分类'), COUNT(bc.id), "
        "SUM(CASE WHEN bc.status = 'available' THEN 1 ELSE 0 END) "
        "FROM books b "
        "LEFT JOIN categories c ON c.id = b.category_id "
        "LEFT JOIN book_copies bc ON bc.book_id = b.id "
        "GROUP BY b.id, b.isbn, b.title, b.author, c.name "
        "ORDER BY b.id DESC");
    if (!query.exec(sql)) {
        if (error) {
            *error = query.lastError().text();
        }
        return result;
    }
    while (query.next()) {
        BookInfo book;
        book.id = query.value(0).toInt();
        book.isbn = query.value(1).toString();
        book.title = query.value(2).toString();
        book.author = query.value(3).toString();
        book.category = query.value(4).toString();
        book.totalCopies = query.value(5).toInt();
        book.availableCopies = query.value(6).toInt();
        result.append(book);
    }
    return result;
}

QStringList BookService::categories(QString *error) const
{
    QStringList result;
    QSqlQuery query(Database::connection());
    if (!query.exec(QStringLiteral("SELECT name FROM categories ORDER BY id"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return result;
    }
    while (query.next()) {
        result.append(query.value(0).toString());
    }
    return result;
}

QVector<AvailableCopyInfo> BookService::availableCopies(QString *error) const
{
    QVector<AvailableCopyInfo> result;
    QSqlQuery query(Database::connection());
    if (!query.exec(QStringLiteral(
            "SELECT bc.id, bc.copy_code, b.title, b.author "
            "FROM book_copies bc "
            "JOIN books b ON b.id = bc.book_id "
            "WHERE bc.status = 'available' "
            "ORDER BY b.title, bc.copy_code"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return result;
    }
    while (query.next()) {
        AvailableCopyInfo copy;
        copy.id = query.value(0).toInt();
        copy.code = query.value(1).toString();
        copy.title = query.value(2).toString();
        copy.author = query.value(3).toString();
        result.append(copy);
    }
    return result;
}
