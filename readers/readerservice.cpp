#include "readerservice.h"

#include "../common/database.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

bool ReaderService::addReader(const UserSession &session,
                              const QString &readerNo,
                              const QString &name,
                              const QString &phone,
                              QString *error)
{
    if (!PermissionService::requirePermission(session, Permission::ManageReaders, error)) {
        return false;
    }
    if (readerNo.trimmed().isEmpty() || name.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("读者编号和姓名不能为空");
        }
        return false;
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "INSERT INTO readers(reader_no, name, phone) VALUES(?, ?, ?)"));
    query.addBindValue(readerNo.trimmed());
    query.addBindValue(name.trimmed());
    query.addBindValue(phone.trimmed());
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().nativeErrorCode().contains(QStringLiteral("1062"))
                         ? QStringLiteral("该读者编号已存在")
                         : query.lastError().text();
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
            "COUNT(l.id), r.active "
            "FROM readers r "
            "LEFT JOIN loans l ON l.reader_id = r.id AND l.returned_at IS NULL "
            "GROUP BY r.id, r.reader_no, r.name, r.phone, r.active "
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
        reader.activeLoans = query.value(4).toInt();
        reader.enabled = query.value(5).toInt() == 1;
        result.append(reader);
    }
    return result;
}
