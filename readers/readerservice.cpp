#include "readerservice.h"

#include "../common/database.h"
#include "../common/operationlogservice.h"
#include "../common/paramservice.h"

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
    if (!PermissionService::requirePermission(
            session,
            Permission::ManageReaders,
            error)) {
        return false;
    }

    const QString normalizedReaderNo =
        readerNo.trimmed();
    const QString normalizedName =
        name.trimmed();
    const QString normalizedPhone =
        phone.trimmed();

    if (normalizedReaderNo.isEmpty()
        || normalizedName.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "读者编号和姓名不能为空");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();
    if (!database.transaction()) {
        if (error) {
            *error = QStringLiteral("新增读者失败");
        }
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO readers "
        "(reader_no, name, phone) "
        "VALUES (?, ?, ?)"));
    query.addBindValue(normalizedReaderNo);
    query.addBindValue(normalizedName);
    query.addBindValue(normalizedPhone);

    if (!query.exec()) {
        const QSqlError sqlError = query.lastError();
        database.rollback();

        if (error) {
            const QString message =
                sqlError.nativeErrorCode()
                + QStringLiteral(" ")
                + sqlError.text();

            *error = message.contains(
                         QStringLiteral("1062"))
                             || message.contains(
                                 QStringLiteral(
                                     "Duplicate entry"),
                                 Qt::CaseInsensitive)
                         ? QStringLiteral(
                               "该读者编号已存在")
                         : QStringLiteral(
                               "新增读者失败");
        }
        return false;
    }

    qlonglong readerId =
        query.lastInsertId().toLongLong();

    if (readerId <= 0) {
        QSqlQuery idQuery(database);
        if (idQuery.exec(QStringLiteral(
                "SELECT LAST_INSERT_ID()"))
            && idQuery.next()) {
            readerId =
                idQuery.value(0).toLongLong();
        }
    }

    OperationLogService logService;
    QString logError;
    if (!logService.record(
            session,
            QStringLiteral("READER_CREATE"),
            QStringLiteral("reader"),
            QString::number(readerId),
            QStringLiteral("新增读者 %1（%2）")
                .arg(normalizedName,
                     normalizedReaderNo),
            &logError)) {
        database.rollback();
        if (error) {
            *error = QStringLiteral(
                "新增读者失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("新增读者失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}

bool ReaderService::updateReader(const UserSession &session,
                                 int readerId,
                                 const QString &readerNo,
                                 const QString &name,
                                 const QString &phone,
                                 QString *error)
{
    if (!PermissionService::requirePermission(
            session, Permission::ManageReaders, error)) {
        return false;
    }

    if (readerId <= 0) {
        if (error) {
            *error = QStringLiteral("无效的读者ID");
        }
        return false;
    }

    const QString normalizedReaderNo = readerNo.trimmed();
    const QString normalizedName = name.trimmed();
    const QString normalizedPhone = phone.trimmed();

    if (normalizedReaderNo.isEmpty() || normalizedName.isEmpty()) {
        if (error) {
            *error = QStringLiteral("读者编号和姓名不能为空");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();
    if (!database.transaction()) {
        if (error) {
            *error = QStringLiteral("修改读者失败");
        }
        return false;
    }

    QSqlQuery currentQuery(database);
    currentQuery.prepare(QStringLiteral(
        "SELECT reader_no, name, COALESCE(phone, '') "
        "FROM readers WHERE id = ? FOR UPDATE"));
    currentQuery.addBindValue(readerId);

    if (!currentQuery.exec()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("修改读者失败");
        }
        return false;
    }

    if (!currentQuery.next()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("读者不存在");
        }
        return false;
    }

    const QString previousReaderNo = currentQuery.value(0).toString();
    const QString previousName = currentQuery.value(1).toString();
    const QString previousPhone = currentQuery.value(2).toString();

    QSqlQuery updateQuery(database);
    updateQuery.prepare(QStringLiteral(
        "UPDATE readers "
        "SET reader_no = ?, name = ?, phone = ? "
        "WHERE id = ?"));
    updateQuery.addBindValue(normalizedReaderNo);
    updateQuery.addBindValue(normalizedName);
    updateQuery.addBindValue(normalizedPhone);
    updateQuery.addBindValue(readerId);

    if (!updateQuery.exec()) {
        const QSqlError sqlError = updateQuery.lastError();
        database.rollback();

        if (error) {
            const QString message =
                sqlError.nativeErrorCode() + QStringLiteral(" ")
                + sqlError.text();

            *error = message.contains(QStringLiteral("1062"))
                             || message.contains(
                                 QStringLiteral("Duplicate entry"),
                                 Qt::CaseInsensitive)
                         ? QStringLiteral("该读者编号已存在")
                         : QStringLiteral("修改读者失败");
        }
        return false;
    }

    OperationLogService logService;
    QString logError;
    if (!logService.record(
            session,
            QStringLiteral("READER_UPDATE"),
            QStringLiteral("reader"),
            QString::number(readerId),
            QStringLiteral(
                "读者编号 %1 -> %2，姓名 %3 -> %4，电话 %5 -> %6")
                .arg(previousReaderNo,
                     normalizedReaderNo,
                     previousName,
                     normalizedName,
                     previousPhone,
                     normalizedPhone),
            &logError)) {
        database.rollback();
        if (error) {
            *error = QStringLiteral(
                "修改读者失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("修改读者失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}

bool ReaderService::setReaderStatus(const UserSession &session,
                                    int readerId,
                                    bool enabled,
                                    QString *error)
{
    if (!PermissionService::requirePermission(
            session, Permission::ManageReaders, error)) {
        return false;
    }

    if (readerId <= 0) {
        if (error) {
            *error = QStringLiteral("无效的读者ID");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();
    if (!database.transaction()) {
        if (error) {
            *error = QStringLiteral("修改读者状态失败");
        }
        return false;
    }

    QSqlQuery readerQuery(database);
    readerQuery.prepare(QStringLiteral(
        "SELECT reader_no, name, active "
        "FROM readers WHERE id = ? FOR UPDATE"));
    readerQuery.addBindValue(readerId);

    if (!readerQuery.exec()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("修改读者状态失败");
        }
        return false;
    }

    if (!readerQuery.next()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("读者不存在");
        }
        return false;
    }

    const QString readerNo = readerQuery.value(0).toString();
    const QString readerName = readerQuery.value(1).toString();
    const bool previousEnabled =
        readerQuery.value(2).toInt() == 1;

    if (previousEnabled == enabled) {
        database.rollback();
        if (error) {
            error->clear();
        }
        return true;
    }

    if (!enabled) {
        QSqlQuery loanQuery(database);
        loanQuery.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM loans "
            "WHERE reader_id = ? AND returned_at IS NULL"));
        loanQuery.addBindValue(readerId);

        if (!loanQuery.exec() || !loanQuery.next()) {
            database.rollback();
            if (error) {
                *error = QStringLiteral("检查读者借阅状态失败");
            }
            return false;
        }

        if (loanQuery.value(0).toInt() > 0) {
            database.rollback();
            if (error) {
                *error = QStringLiteral(
                    "读者有未归还图书，不能停用");
            }
            return false;
        }
    }

    QSqlQuery updateQuery(database);
    updateQuery.prepare(QStringLiteral(
        "UPDATE readers SET active = ? WHERE id = ?"));
    updateQuery.addBindValue(enabled ? 1 : 0);
    updateQuery.addBindValue(readerId);

    if (!updateQuery.exec()
        || updateQuery.numRowsAffected() != 1) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("修改读者状态失败");
        }
        return false;
    }

    OperationLogService logService;
    QString logError;
    if (!logService.record(
            session,
            QStringLiteral("READER_STATUS_UPDATE"),
            QStringLiteral("reader"),
            QString::number(readerId),
            QStringLiteral("读者 %1（%2）状态：%3 -> %4")
                .arg(readerName,
                     readerNo,
                     previousEnabled
                         ? QStringLiteral("启用")
                         : QStringLiteral("停用"),
                     enabled
                         ? QStringLiteral("启用")
                         : QStringLiteral("停用")),
            &logError)) {
        database.rollback();
        if (error) {
            *error = QStringLiteral(
                "修改读者状态失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("修改读者状态失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }
    return true;
}

ReaderEligibilityResult ReaderService::checkReaderEligibility(
    const UserSession &session,
    int readerId,
    QString *error) const
{
    ReaderEligibilityResult result;

    if (!PermissionService::requirePermission(
            session,
            Permission::ManageCirculation,
            error)) {
        return result;
    }

    if (readerId <= 0) {
        if (error) {
            *error = QStringLiteral("无效的读者ID");
        }
        return result;
    }

    ParamService paramService;
    const auto borrowLimit =
        paramService.getIntParam(
            QStringLiteral("borrow_limit"),
            error);

    if (!borrowLimit.has_value()) {
        return result;
    }

    if (*borrowLimit <= 0) {
        if (error) {
            *error = QStringLiteral("借阅上限配置无效");
        }
        return result;
    }

    result.borrowLimit = *borrowLimit;

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT "
        "r.active, "
        "COALESCE(u.active, 1), "
        "COUNT(l.id) "
        "FROM readers r "
        "LEFT JOIN users u ON u.id = r.user_id "
        "LEFT JOIN loans l "
        "ON l.reader_id = r.id "
        "AND l.returned_at IS NULL "
        "WHERE r.id = ? "
        "GROUP BY r.id, r.active, u.active"));
    query.addBindValue(readerId);

    if (!query.exec()) {
        if (error) {
            *error = QStringLiteral("检查读者借阅资格失败");
        }
        return result;
    }

    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("读者不存在");
        }
        return result;
    }

    result.readerEnabled =
        query.value(0).toInt() == 1;
    result.accountEnabled =
        query.value(1).toInt() == 1;
    result.activeLoans =
        query.value(2).toInt();

    if (!result.readerEnabled) {
        result.reason = QStringLiteral("读者已停用");
    } else if (!result.accountEnabled) {
        result.reason = QStringLiteral("关联账号已停用");
    } else if (result.activeLoans >= result.borrowLimit) {
        result.reason = QStringLiteral("已达到借阅上限");
    } else {
        result.eligible = true;
    }

    if (error) {
        error->clear();
    }

    return result;
}

QVector<ReaderInfo> ReaderService::readers(
    const UserSession &session,
    QString *error) const
{
    QVector<ReaderInfo> result;

    if (!PermissionService::requirePermission(
            session,
            Permission::ViewReaders,
            error)) {
        return result;
    }

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
    if (error) {
        error->clear();
    }

    return result;
}