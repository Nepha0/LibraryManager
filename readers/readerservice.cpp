#include "readerservice.h"

#include "../common/database.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

bool ReaderService::addReader(const UserSession &session,
                              const QString &readerNo,
                              const QString &name,
                              const QString &phone,
                              const QString &type,
                              QString *error)
{
    if (!session.isAdmin()) {
        if (error) {
            *error = QStringLiteral("只有管理员可以新增读者");
        }
        return false;
    }
    if (readerNo.trimmed().isEmpty() || name.trimmed().isEmpty()
        || type.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("读者编号、姓名和类型不能为空");
        }
        return false;
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "INSERT INTO readers("
        "reader_no, name, phone, type_id, active, created_at) "
        "SELECT ?, ?, ?, id, 1, ? FROM reader_types WHERE name = ?"));
    query.addBindValue(readerNo.trimmed());
    query.addBindValue(name.trimmed());
    query.addBindValue(phone.trimmed());
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(type);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().nativeErrorCode() == QStringLiteral("19")
                         ? QStringLiteral("该读者编号已存在")
                         : query.lastError().text();
        }
        return false;
    }
    if (query.numRowsAffected() != 1) {
        if (error) {
            *error = QStringLiteral("所选读者类型不存在");
        }
        return false;
    }
    return true;
}

QVector<ReaderInfo> ReaderService::readers(QString *error) const
{
    QVector<ReaderInfo> result;
    QSqlQuery query(Database::connection());
    if (!query.exec(QStringLiteral(
            "SELECT r.id, r.reader_no, r.name, COALESCE(r.phone, ''), "
            "rt.name, COUNT(l.id), r.active "
            "FROM readers r "
            "JOIN reader_types rt ON rt.id = r.type_id "
            "LEFT JOIN loans l ON l.reader_id = r.id AND l.returned_at IS NULL "
            "GROUP BY r.id, r.reader_no, r.name, r.phone, rt.name, r.active "
            "ORDER BY r.id DESC"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return result;
    }
    while (query.next()) {
        ReaderInfo reader;
        reader.id = query.value(0).toInt();
        reader.readerNo = query.value(1).toString();
        reader.name = query.value(2).toString();
        reader.phone = query.value(3).toString();
        reader.type = query.value(4).toString();
        reader.activeLoans = query.value(5).toInt();
        reader.enabled = query.value(6).toInt() == 1;
        result.append(reader);
    }
    return result;
}

QStringList ReaderService::readerTypes(QString *error) const
{
    QStringList result;
    QSqlQuery query(Database::connection());
    if (!query.exec(QStringLiteral(
            "SELECT name FROM reader_types ORDER BY max_loans"))) {
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
