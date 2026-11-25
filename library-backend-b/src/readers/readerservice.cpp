#include "readerservice.h"

#include "../system/operationlogservice.h"
#include "../system/paramservice.h"
#include "common/database.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

lib::Result readerFailure(const QString &error)
{
    if (error == QStringLiteral("请先登录")
        || error == QStringLiteral("登录状态已失效，请重新登录")) {
        return lib::Result::fail(lib::ErrorCode::NotAuthenticated, error);
    }
    if (error == QStringLiteral("无权限")) {
        return lib::Result::fail(lib::ErrorCode::NoPermission, error);
    }
    if (error == QStringLiteral("读者不存在")) {
        return lib::failB(lib::ErrorCodeB::ReaderNotFound, error);
    }
    if (error == QStringLiteral("无效的读者ID")) {
        return lib::failB(lib::ErrorCodeB::InvalidReaderId, error);
    }
    if (error == QStringLiteral("该读者编号已存在")) {
        return lib::failB(lib::ErrorCodeB::ReaderNoDuplicate, error);
    }
    if (error == QStringLiteral("读者有未归还图书，不能停用")) {
        return lib::failB(lib::ErrorCodeB::ReaderHasBorrowRecords, error);
    }
    return lib::Result::fail(
        lib::ErrorCode::DbError,
        error.isEmpty() ? QStringLiteral("读者操作失败") : error);
}

QVariantMap readerToMap(const ReaderInfo &reader)
{
    QVariantMap value;
    value.insert(QStringLiteral("reader_id"), reader.id);
    value.insert(QStringLiteral("reader_no"), reader.readerNo);
    value.insert(QStringLiteral("name"), reader.name);
    value.insert(QStringLiteral("phone"), reader.phone);
    value.insert(QStringLiteral("active_loans"), reader.activeLoans);
    value.insert(QStringLiteral("enabled"), reader.enabled);
    return value;
}

QVariantMap readerDetailToMap(const ReaderDetail &detail)
{
    QVariantMap value = readerToMap(detail.info);
    value.insert(QStringLiteral("total_loans"), detail.totalLoans);
    value.insert(QStringLiteral("returned_loans"), detail.returnedLoans);
    value.insert(QStringLiteral("overdue_loans"), detail.overdueLoans);
    // 这些字段由 C 侧在联调时补充当前预约和实时逾期聚合；B 先保留稳定的契约位。
    value.insert(QStringLiteral("current_loan_count"), detail.info.activeLoans);
    value.insert(QStringLiteral("current_reservation_count"), 0);
    value.insert(QStringLiteral("overdue_count"), detail.overdueLoans);
    return value;
}

}

lib::Result ReaderService::addReader(
    const lib::OperatorContext &context,
    const QVariantMap &fields)
{
    const QString readerNo = fields.value(QStringLiteral("reader_no"),
                                           fields.value(QStringLiteral("readerNo")))
                                 .toString();
    const QString name = fields.value(QStringLiteral("name"))
                             .toString();
    const QString phone = fields.value(QStringLiteral("phone"))
                              .toString();
    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    if (!addReader(session, readerNo, name, phone, &error)) {
        return readerFailure(error);
    }
    QVariantMap data;
    data.insert(QStringLiteral("reader_no"), readerNo.trimmed());
    data.insert(QStringLiteral("name"), name.trimmed());
    data.insert(QStringLiteral("phone"), phone.trimmed());
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("新增读者成功"),
                       data,
                       {},
                       1};
}

lib::Result ReaderService::updateReader(
    const lib::OperatorContext &context,
    qint64 readerId,
    const QVariantMap &fields)
{
    const QString readerNo = fields.value(QStringLiteral("reader_no"),
                                           fields.value(QStringLiteral("readerNo")))
                                 .toString();
    const QString name = fields.value(QStringLiteral("name"))
                             .toString();
    const QString phone = fields.value(QStringLiteral("phone"))
                              .toString();
    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    if (!updateReader(session, static_cast<int>(readerId), readerNo, name,
                      phone, &error)) {
        return readerFailure(error);
    }
    QVariantMap data;
    data.insert(QStringLiteral("reader_id"), readerId);
    data.insert(QStringLiteral("reader_no"), readerNo.trimmed());
    data.insert(QStringLiteral("name"), name.trimmed());
    data.insert(QStringLiteral("phone"), phone.trimmed());
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("读者资料已更新"),
                       data,
                       {},
                       1};
}

lib::Result ReaderService::setReaderStatus(
    const lib::OperatorContext &context,
    qint64 readerId,
    bool enabled)
{
    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    if (!setReaderStatus(session, static_cast<int>(readerId), enabled,
                         &error)) {
        return readerFailure(error);
    }
    QVariantMap data;
    data.insert(QStringLiteral("reader_id"), readerId);
    data.insert(QStringLiteral("enabled"), enabled);
    return lib::Result{lib::ErrorCode::Success,
                       enabled ? QStringLiteral("读者已启用")
                               : QStringLiteral("读者已停用"),
                       data,
                       {},
                       1};
}

lib::Result ReaderService::checkReaderEligibility(
    const lib::OperatorContext &context,
    qint64 readerId) const
{
    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    const ReaderEligibilityResult eligibility =
        checkReaderEligibility(session, static_cast<int>(readerId), &error);
    if (!error.isEmpty()) {
        return readerFailure(error);
    }
    QVariantMap data;
    data.insert(QStringLiteral("reader_id"), readerId);
    data.insert(QStringLiteral("eligible"), eligibility.eligible);
    data.insert(QStringLiteral("reader_enabled"), eligibility.readerEnabled);
    data.insert(QStringLiteral("account_enabled"), eligibility.accountEnabled);
    data.insert(QStringLiteral("active_loans"), eligibility.activeLoans);
    data.insert(QStringLiteral("borrow_limit"), eligibility.borrowLimit);
    QVariantList reasons;
    for (const QString &reason : eligibility.reasons) {
        reasons.append(reason);
    }
    data.insert(QStringLiteral("reasons"), reasons);
    return lib::Result{lib::ErrorCode::Success,
                       eligibility.reason,
                       data,
                       {},
                       1};
}

lib::Result ReaderService::checkReaderEligibility(qint64 readerId) const
{
    if (readerId <= 0) {
        return lib::failB(lib::ErrorCodeB::InvalidReaderId,
                          QStringLiteral("无效的读者ID"));
    }

    ParamService paramService;
    QString error;
    const auto borrowLimit = paramService.getIntParam(
        QStringLiteral("borrow_limit"), &error);
    if (!borrowLimit.has_value()) {
        return readerFailure(error);
    }
    if (*borrowLimit <= 0) {
        return lib::failB(lib::ErrorCodeB::InvalidParamValue,
                          QStringLiteral("借阅上限配置无效"));
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT r.active, COALESCE(u.active, 1), COUNT(l.id) "
        "FROM readers r "
        "LEFT JOIN users u ON u.id = r.user_id "
        "LEFT JOIN loans l ON l.reader_id = r.id "
        "AND l.returned_at IS NULL "
        "WHERE r.id = ? "
        "GROUP BY r.id, r.active, u.active"));
    query.addBindValue(readerId);
    if (!query.exec()) {
        return lib::Result::fail(
            lib::ErrorCode::DbError,
            QStringLiteral("检查读者借阅资格失败"));
    }
    if (!query.next()) {
        return lib::failB(lib::ErrorCodeB::ReaderNotFound,
                          QStringLiteral("读者不存在"));
    }

    const bool readerEnabled = query.value(0).toInt() == 1;
    const bool accountEnabled = query.value(1).toInt() == 1;
    const int activeLoans = query.value(2).toInt();
    QStringList reasons;
    if (!readerEnabled) {
        reasons.append(QStringLiteral("读者已停用"));
    }
    if (!accountEnabled) {
        reasons.append(QStringLiteral("关联账号已停用"));
    }
    if (activeLoans >= *borrowLimit) {
        reasons.append(QStringLiteral("已达到借阅上限"));
    }

    QVariantMap data;
    data.insert(QStringLiteral("reader_id"), readerId);
    data.insert(QStringLiteral("eligible"), reasons.isEmpty());
    data.insert(QStringLiteral("reader_enabled"), readerEnabled);
    data.insert(QStringLiteral("account_enabled"), accountEnabled);
    data.insert(QStringLiteral("active_loans"), activeLoans);
    data.insert(QStringLiteral("borrow_limit"), *borrowLimit);
    QVariantList reasonList;
    for (const QString &reason : reasons) {
        reasonList.append(reason);
    }
    data.insert(QStringLiteral("reasons"), reasonList);
    return lib::Result{lib::ErrorCode::Success,
                       reasons.join(QStringLiteral("；")),
                       data,
                       {},
                       1};
}

lib::Result ReaderService::getReader(
    const lib::OperatorContext &context,
    qint64 readerId) const
{
    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    const auto detail = getReader(session, static_cast<int>(readerId), &error);
    if (!detail.has_value()) {
        return readerFailure(error);
    }
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("查询读者详情成功"),
                       readerDetailToMap(*detail),
                       {},
                       1};
}

lib::Result ReaderService::listReaders(
    const lib::OperatorContext &context,
    const QVariantMap &filter,
    int page,
    int pageSize) const
{
    if (!lib::pageParamsValid(page, pageSize)) {
        return lib::Result::fail(lib::ErrorCode::InvalidPage,
                                 QStringLiteral("分页参数非法"));
    }

    ReaderQuery query;
    query.readerNo = filter.value(QStringLiteral("reader_no"),
                                  filter.value(QStringLiteral("readerNo")))
                         .toString();
    query.exactReaderNo = filter.value(QStringLiteral("exact_reader_no")).toBool();
    query.name = filter.value(QStringLiteral("name")).toString();
    if (filter.contains(QStringLiteral("enabled"))) {
        query.enabled = filter.value(QStringLiteral("enabled")).toBool();
    }

    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    const QVector<ReaderInfo> all = readers(session, query, &error);
    if (!error.isEmpty()) {
        return readerFailure(error);
    }

    const int total = all.size();
    const qint64 offset = qint64(page - 1) * pageSize;
    const qint64 end = qMin<qint64>(offset + pageSize, total);
    QVariantList list;
    for (qint64 index = offset; index < end;
         ++index) {
        list.append(readerToMap(all.at(index)));
    }
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("查询读者成功"),
                       {},
                       list,
                       total};
}

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
        result.reasons.append(
            QStringLiteral("读者已停用"));
    }

    if (!result.accountEnabled) {
        result.reasons.append(
            QStringLiteral("关联账号已停用"));
    }

    if (result.activeLoans >= result.borrowLimit) {
        result.reasons.append(
            QStringLiteral("已达到借阅上限"));
    }

    result.eligible = result.reasons.isEmpty();
    result.reason = result.reasons.join(
        QStringLiteral("；"));

    if (error) {
        error->clear();
    }

    return result;
}

std::optional<ReaderDetail> ReaderService::getReader(
    const UserSession &session,
    int readerId,
    QString *error) const
{
    if (!PermissionService::requirePermission(
            session,
            Permission::ViewReaders,
            error)) {
        return std::nullopt;
    }

    if (readerId <= 0) {
        if (error) {
            *error = QStringLiteral("无效的读者ID");
        }
        return std::nullopt;
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT "
        "r.id, r.reader_no, r.name, "
        "COALESCE(r.phone, ''), r.active, "
        "COUNT(l.id), "
        "SUM(CASE WHEN l.id IS NOT NULL "
        "AND l.returned_at IS NULL THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN l.id IS NOT NULL "
        "AND l.returned_at IS NOT NULL THEN 1 ELSE 0 END), "
        "SUM(CASE WHEN l.id IS NOT NULL "
        "AND l.returned_at IS NULL "
        "AND l.due_at < CURRENT_TIMESTAMP "
        "THEN 1 ELSE 0 END) "
        "FROM readers r "
        "LEFT JOIN loans l ON l.reader_id = r.id "
        "WHERE r.id = ? "
        "GROUP BY r.id, r.reader_no, r.name, "
        "r.phone, r.active"));
    query.addBindValue(readerId);

    if (!query.exec()) {
        if (error) {
            *error = QStringLiteral("查询读者详情失败");
        }
        return std::nullopt;
    }

    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("读者不存在");
        }
        return std::nullopt;
    }

    ReaderDetail detail;
    detail.info.id = query.value(0).toInt();
    detail.info.readerNo = query.value(1).toString();
    detail.info.name = query.value(2).toString();
    detail.info.phone = query.value(3).toString();
    detail.info.enabled = query.value(4).toInt() == 1;
    detail.totalLoans = query.value(5).toInt();
    detail.info.activeLoans = query.value(6).toInt();
    detail.returnedLoans = query.value(7).toInt();
    detail.overdueLoans = query.value(8).toInt();

    if (error) {
        error->clear();
    }

    return detail;
}

QVector<ReaderInfo> ReaderService::readers(
    const UserSession &session,
    const ReaderQuery &criteria,
    QString *error) const
{
    QVector<ReaderInfo> result;

    if (!PermissionService::requirePermission(
            session,
            Permission::ViewReaders,
            error)) {
        return result;
    }

    QString sql = QStringLiteral(
        "SELECT "
        "r.id, r.reader_no, r.name, "
        "COALESCE(r.phone, ''), "
        "COUNT(l.id), r.active "
        "FROM readers r "
        "LEFT JOIN loans l "
        "ON l.reader_id = r.id "
        "AND l.returned_at IS NULL "
        "WHERE 1 = 1 ");

    const QString readerNo =
        criteria.readerNo.trimmed();
    const QString name =
        criteria.name.trimmed();

    if (!readerNo.isEmpty()) {
        sql += criteria.exactReaderNo
            ? QStringLiteral("AND r.reader_no = :reader_no ")
            : QStringLiteral("AND r.reader_no LIKE :reader_no ");
    }

    if (!name.isEmpty()) {
        sql += QStringLiteral(
            "AND r.name LIKE :name ");
    }

    if (criteria.enabled.has_value()) {
        sql += QStringLiteral(
            "AND r.active = :active ");
    }

    sql += QStringLiteral(
        "GROUP BY r.id, r.reader_no, r.name, "
        "r.phone, r.active "
        "ORDER BY r.id DESC");

    QSqlQuery query(Database::connection());
    query.prepare(sql);

    if (!readerNo.isEmpty()) {
        query.bindValue(
            QStringLiteral(":reader_no"),
            criteria.exactReaderNo
                ? readerNo
                : QStringLiteral("%") + readerNo + QStringLiteral("%"));
    }

    if (!name.isEmpty()) {
        query.bindValue(
            QStringLiteral(":name"),
            QStringLiteral("%") + name
                + QStringLiteral("%"));
    }

    if (criteria.enabled.has_value()) {
        query.bindValue(
            QStringLiteral(":active"),
            *criteria.enabled ? 1 : 0);
    }

    if (!query.exec()) {
        if (error) {
            *error = QStringLiteral("查询读者列表失败");
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

QVector<ReaderInfo> ReaderService::readers(
    const UserSession &session,
    QString *error) const
{
    return readers(
        session,
        ReaderQuery{},
        error);
}
