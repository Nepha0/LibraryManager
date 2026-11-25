#include "BookService.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDate>
#include "../database/DatabaseManager.h"

namespace lib {

BookService::BookService(QSqlDatabase db) : m_db(db) {}

// ===================== 私有：字段校验与规范化 =====================
Result BookService::validateFields(const QVariantMap& fields, bool forCreate,
                                   qint64 excludeBookId, QVariantMap* normalized)
{
    QVariantMap out;
    if (normalized) *normalized = QVariantMap();

    // ---- ISBN ----
    QString isbn = fields.value(QStringLiteral("isbn")).toString().trimmed();
    if (forCreate) {
        if (isbn.isEmpty())
            return Result::fail(ErrorCode::MissingField, QStringLiteral("必填字段缺失：ISBN"));
        out.insert(QStringLiteral("isbn"), isbn);
    } else if (fields.contains(QStringLiteral("isbn"))) {
        if (isbn.isEmpty())
            return Result::fail(ErrorCode::MissingField, QStringLiteral("ISBN 不能为空"));
        out.insert(QStringLiteral("isbn"), isbn);
    }

    // ---- 书名 ----
    QString title = fields.value(QStringLiteral("title")).toString().trimmed();
    if (forCreate) {
        if (title.isEmpty())
            return Result::fail(ErrorCode::MissingField, QStringLiteral("必填字段缺失：书名"));
        out.insert(QStringLiteral("title"), title);
    } else if (fields.contains(QStringLiteral("title"))) {
        if (title.isEmpty())
            return Result::fail(ErrorCode::MissingField, QStringLiteral("书名不能为空"));
        out.insert(QStringLiteral("title"), title);
    }

    // ---- 分类外键（新增时必填） ----
    if (fields.contains(QStringLiteral("category_id")) || forCreate) {
        bool convOk = false;
        qint64 categoryId = fields.value(QStringLiteral("category_id")).toLongLong(&convOk);
        if (!convOk || categoryId <= 0)
            return Result::fail(ErrorCode::MissingField, QStringLiteral("必填字段缺失：分类"));
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT name FROM categories WHERE id = ?"));
        q.addBindValue(categoryId);
        if (!q.exec() || !q.next())
            return Result::fail(ErrorCode::ForeignKeyNotFound,
                                QStringLiteral("分类不存在（category_id=%1）").arg(categoryId));
        out.insert(QStringLiteral("category_id"), categoryId);
    }

    // ---- 出版日期（可选，格式校验） ----
    if (fields.contains(QStringLiteral("publish_date"))) {
        QString d = fields.value(QStringLiteral("publish_date")).toString().trimmed();
        if (!d.isEmpty() && QDate::fromString(d, QStringLiteral("yyyy-MM-dd")).isNull())
            return Result::fail(ErrorCode::InvalidCondition,
                                QStringLiteral("出版日期格式应为 yyyy-MM-dd"));
        out.insert(QStringLiteral("publish_date"), d);
    }

    // ---- 其余可选字段 ----
    static const QStringList textKeys = {
        QStringLiteral("author"), QStringLiteral("publisher"),
        QStringLiteral("intro"),  QStringLiteral("cover_path")
    };
    for (const QString& k : textKeys)
        if (fields.contains(k))
            out.insert(k, fields.value(k).toString().trimmed());

    // ---- ISBN 唯一校验（ACTIVE 范围内；排除自身与已下架图书） ----
    if (out.contains(QStringLiteral("isbn"))) {
        QSqlQuery q(m_db);
        QString sql = QStringLiteral(
            "SELECT COUNT(*) FROM books WHERE isbn = ?");
        if (excludeBookId > 0)
            sql += QStringLiteral(" AND id <> %1").arg(excludeBookId);
        q.prepare(sql);
        q.addBindValue(out.value(QStringLiteral("isbn")));
        if (!q.exec()) {
            return Result::fail(ErrorCode::DbError,
                                QStringLiteral("ISBN 查重失败: %1").arg(q.lastError().text()));
        }
        q.next();
        if (q.value(0).toInt() > 0)
            return Result::fail(ErrorCode::IsbnDuplicate,
                                QStringLiteral("ISBN %1 已存在").arg(out.value(QStringLiteral("isbn")).toString()));
    }

    if (normalized) *normalized = out;
    return Result::success();
}

// ===================== 新增图书 =====================
Result BookService::addBook(const OperatorContext& op, const QVariantMap& fields)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可新增图书"));

    QVariantMap norm;
    Result v = validateFields(fields, true, 0, &norm);
    if (!v.ok()) return v;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO books (isbn, title, author, publisher, publish_date, category_id,"
        " intro, cover_path, status) VALUES (?,?,?,?,?,?,?,?, 'active')"));
    q.addBindValue(norm.value(QStringLiteral("isbn")));
    q.addBindValue(norm.value(QStringLiteral("title")));
    q.addBindValue(norm.value(QStringLiteral("author"), QString()));
    q.addBindValue(norm.value(QStringLiteral("publisher"), QString()));
    q.addBindValue(norm.value(QStringLiteral("publish_date"), QString()));
    q.addBindValue(norm.value(QStringLiteral("category_id")));
    q.addBindValue(norm.value(QStringLiteral("intro"), QString()));
    q.addBindValue(norm.value(QStringLiteral("cover_path"), QString()));
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("新增图书失败: %1").arg(q.lastError().text()));
    return fetchBookRow(q.lastInsertId().toLongLong(), nullptr);
}

// ===================== 更新图书 =====================
Result BookService::updateBook(const OperatorContext& op, qint64 bookId, const QVariantMap& fields)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可编辑图书"));

    QVariantMap existing;
    Result f = fetchBookRow(bookId, &existing);
    if (!f.ok()) return f;

    QVariantMap norm;
    Result v = validateFields(fields, false, bookId, &norm);
    if (!v.ok()) return v;
    if (norm.isEmpty())
        return Result::fail(ErrorCode::InvalidCondition, QStringLiteral("未提供任何可更新的字段"));

    QStringList sets;
    QVariantList binds;
    const QStringList keys = norm.keys();
    for (const QString& k : keys) {
        sets << QStringLiteral("%1 = ?").arg(k);
        binds << norm.value(k);
    }
    sets << QStringLiteral("updated_at = ") + DatabaseManager::instance().nowExpr();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE books SET %1 WHERE id = %2")
                  .arg(sets.join(QStringLiteral(", "))).arg(bookId));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("更新图书失败: %1").arg(q.lastError().text()));
    return fetchBookRow(bookId, nullptr);
}

// ===================== 副本统计（getBook 与列表页共用） =====================
static QVariantMap copyStats(QSqlDatabase db, qint64 bookId)
{
    QVariantMap stats;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT status, COUNT(*) FROM book_copies WHERE book_id = ? GROUP BY status"));
    q.addBindValue(bookId);
    q.exec();
    const QStringList keys = {
        QStringLiteral("copy_available"), QStringLiteral("copy_borrowed"),
        QStringLiteral("copy_reserved"),  QStringLiteral("copy_maintenance"),
        QStringLiteral("copy_lost")
    };
    for (const QString& k : keys) stats.insert(k, 0);
    int total = 0;
    while (q.next()) {
        const QString st = q.value(0).toString();
        const int n = q.value(1).toInt();
        total += n;
        if (st.compare(QStringLiteral("available"), Qt::CaseInsensitive) == 0) stats[QStringLiteral("copy_available")] = n;
        else if (st.compare(QStringLiteral("borrowed"), Qt::CaseInsensitive) == 0) stats[QStringLiteral("copy_borrowed")] = n;
        else if (st.compare(QStringLiteral("reserved"), Qt::CaseInsensitive) == 0) stats[QStringLiteral("copy_reserved")] = n;
        else if (st.compare(QStringLiteral("maintenance"), Qt::CaseInsensitive) == 0) stats[QStringLiteral("copy_maintenance")] = n;
        else if (st.compare(QStringLiteral("lost"), Qt::CaseInsensitive) == 0) stats[QStringLiteral("copy_lost")] = n;
    }
    stats.insert(QStringLiteral("copy_total"), total);

    // 馆藏位置（去重）
    QStringList locations;
    QSqlQuery q2(db);
    q2.prepare(QStringLiteral(
        "SELECT DISTINCT location FROM book_copies WHERE book_id = ? AND location <> '' ORDER BY location"));
    q2.addBindValue(bookId);
    q2.exec();
    while (q2.next()) locations << q2.value(0).toString();
    stats.insert(QStringLiteral("locations"), locations);
    stats.insert(QStringLiteral("locations_text"), locations.join(QStringLiteral("、")));
    return stats;
}

Result BookService::fetchBookRow(qint64 bookId, QVariantMap* out)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT b.id, b.isbn, b.title, b.author, b.publisher, b.publish_date,"
        " b.category_id, c.name AS category_name, b.intro, b.cover_path, b.status, b.created_at"
        " FROM books b LEFT JOIN categories c ON c.id = b.category_id WHERE b.id = ?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::BookNotFound,
                            QStringLiteral("图书不存在（id=%1）").arg(bookId));
    QVariantMap book;
    book.insert(QStringLiteral("book_id"),        q.value(QStringLiteral("id")));
    book.insert(QStringLiteral("isbn"),           q.value(QStringLiteral("isbn")));
    book.insert(QStringLiteral("title"),          q.value(QStringLiteral("title")));
    book.insert(QStringLiteral("author"),         q.value(QStringLiteral("author")));
    book.insert(QStringLiteral("publisher"),      q.value(QStringLiteral("publisher")));
    book.insert(QStringLiteral("publish_date"),   q.value(QStringLiteral("publish_date")));
    book.insert(QStringLiteral("category_id"),    q.value(QStringLiteral("category_id")));
    book.insert(QStringLiteral("category_name"),  q.value(QStringLiteral("category_name")));
    book.insert(QStringLiteral("intro"),          q.value(QStringLiteral("intro")));
    book.insert(QStringLiteral("cover_path"),     q.value(QStringLiteral("cover_path")));
    book.insert(QStringLiteral("status"),         q.value(QStringLiteral("status")));
    book.insert(QStringLiteral("status_text"),    bookStatusDisplayName(q.value(QStringLiteral("status")).toString()));
    if (out) *out = book;

    Result r = Result::success();
    r.data = book;
    QVariantMap stats = copyStats(m_db, bookId);
    for (auto it = stats.begin(); it != stats.end(); ++it)
        r.data.insert(it.key(), it.value());
    return r;
}

// ===================== 图书详情 =====================
Result BookService::getBook(const OperatorContext& op, qint64 bookId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    QVariantMap tmp;
    return fetchBookRow(bookId, &tmp);
}

// ===================== 分页筛选 =====================
Result BookService::listBooks(const OperatorContext& op, const QVariantMap& filter,
                              int page, int pageSize)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (!pageParamsValid(page, pageSize))
        return Result::fail(ErrorCode::InvalidPage,
                            QStringLiteral("分页参数非法：page=%1, pageSize=%2").arg(page).arg(pageSize));

    QStringList where;
    QVariantList binds;
    const QString keyword = filter.value(QStringLiteral("keyword")).toString().trimmed();
    if (!keyword.isEmpty()) {
        where << QStringLiteral("(b.title LIKE ? OR b.author LIKE ? OR b.isbn LIKE ?)");
        const QString like = QStringLiteral("%%1%").arg(keyword);
        binds << like << like << like;
    }
    if (filter.contains(QStringLiteral("category_id")) && !filter.value(QStringLiteral("category_id")).isNull()) {
        bool convOk = false;
        qint64 cid = filter.value(QStringLiteral("category_id")).toLongLong(&convOk);
        if (convOk && cid > 0) {
            where << QStringLiteral("b.category_id = ?");
            binds << cid;
        }
    }
    const QString publisher = filter.value(QStringLiteral("publisher")).toString().trimmed();
    if (!publisher.isEmpty()) {
        where << QStringLiteral("b.publisher LIKE ?");
        binds << QStringLiteral("%%1%").arg(publisher);
    }
    const QString status = filter.value(QStringLiteral("status")).toString().trimmed();
    bool stOk = false;
    const BookStatus st = bookStatusFromString(status, &stOk);
    if (stOk) {
        where << QStringLiteral("b.status = ?");
        binds << bookStatusToString(st);
    }
    const QString whereSql =
        where.isEmpty() ? QString() : QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM books b%1").arg(whereSql));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("统计图书总数失败: %1").arg(q.lastError().text()));
    Result r;
    r.total = q.value(0).toInt();

    q.prepare(QStringLiteral(
        "SELECT b.id, b.isbn, b.title, b.author, b.publisher, b.publish_date,"
        " b.category_id, c.name AS category_name, b.status"
        " FROM books b LEFT JOIN categories c ON c.id = b.category_id%1"
        " ORDER BY b.id DESC LIMIT %2 OFFSET %3")
        .arg(whereSql)
        .arg(pageSize)
        .arg((page - 1) * pageSize));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("查询图书列表失败: %1").arg(q.lastError().text()));

    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("book_id"),       q.value(QStringLiteral("id")));
        row.insert(QStringLiteral("isbn"),          q.value(QStringLiteral("isbn")));
        row.insert(QStringLiteral("title"),         q.value(QStringLiteral("title")));
        row.insert(QStringLiteral("author"),        q.value(QStringLiteral("author")));
        row.insert(QStringLiteral("publisher"),     q.value(QStringLiteral("publisher")));
        row.insert(QStringLiteral("publish_date"),  q.value(QStringLiteral("publish_date")));
        row.insert(QStringLiteral("category_id"),   q.value(QStringLiteral("category_id")));
        row.insert(QStringLiteral("category_name"), q.value(QStringLiteral("category_name")));
        row.insert(QStringLiteral("status"),        q.value(QStringLiteral("status")));
        row.insert(QStringLiteral("status_text"),
                   bookStatusDisplayName(q.value(QStringLiteral("status")).toString()));
        QVariantMap stats = copyStats(m_db, q.value(QStringLiteral("id")).toLongLong());
        row.insert(QStringLiteral("copy_total"),     stats.value(QStringLiteral("copy_total")));
        row.insert(QStringLiteral("copy_available"), stats.value(QStringLiteral("copy_available")));
        r.list << row;
    }
    return r;
}

// ===================== 下架（逻辑删除） =====================
Result BookService::shelveOff(const OperatorContext& op, qint64 bookId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可下架图书"));

    QVariantMap existing;
    Result f = fetchBookRow(bookId, &existing);
    if (!f.ok()) return f;
    if (existing.value(QStringLiteral("status")).toString().compare(QStringLiteral("shelved_off"), Qt::CaseInsensitive) == 0)
        return Result::fail(ErrorCode::BookShelvedOff, QStringLiteral("该图书已处于下架状态"));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE books SET status = 'shelved_off', updated_at = ")
                  + DatabaseManager::instance().nowExpr()
                  + QStringLiteral(" WHERE id = ?"));
    q.addBindValue(bookId);
    if (!q.exec() || q.numRowsAffected() != 1)
        return Result::fail(ErrorCode::DbError, QStringLiteral("下架图书失败"));
    return Result::success(QStringLiteral("图书《%1》已下架").arg(existing.value(QStringLiteral("title")).toString()));
}

// ===================== 物理删除（有借阅记录则拒绝） =====================
Result BookService::deleteBook(const OperatorContext& op, qint64 bookId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可删除图书"));

    QVariantMap existing;
    Result f = fetchBookRow(bookId, &existing);
    if (!f.ok()) return f;

    // 该图书存在借阅记录 → 拒绝物理删除，提示改用下架
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM loans WHERE book_id = ?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("借阅记录检查失败"));
    if (q.value(0).toInt() > 0)
        return Result::fail(ErrorCode::HasBorrowRecords,
                            QStringLiteral("该图书存在借阅记录，请使用下架（逻辑删除）"));

    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError, QStringLiteral("事务开启失败"));
    QSqlQuery d1(m_db);
    d1.prepare(QStringLiteral("DELETE FROM book_copies WHERE book_id = ?"));
    d1.addBindValue(bookId);
    QSqlQuery d2(m_db);
    d2.prepare(QStringLiteral("DELETE FROM books WHERE id = ?"));
    d2.addBindValue(bookId);
    if (!d1.exec() || !d2.exec()) {
        m_db.rollback();
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("删除图书失败: %1").arg(d2.lastError().text()));
    }
    m_db.commit();
    return Result::success(QStringLiteral("图书《%1》及其副本已删除").arg(existing.value(QStringLiteral("title")).toString()));
}

} // namespace lib
