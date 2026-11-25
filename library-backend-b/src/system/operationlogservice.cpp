#include "operationlogservice.h"

#include "../auth/authservice.h"
#include "../auth/permissionservice.h"
#include "common/database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QVariantList>

namespace {

lib::Result logFailure(const QString &error)
{
    if (error == QStringLiteral("请先登录")
        || error == QStringLiteral("登录状态已失效，请重新登录")) {
        return lib::Result::fail(lib::ErrorCode::NotAuthenticated, error);
    }
    if (error == QStringLiteral("无权限")) {
        return lib::failB(lib::ErrorCodeB::LogQueryDenied, error);
    }
    if (error == QStringLiteral("开始时间不能晚于结束时间")) {
        return lib::Result::fail(lib::ErrorCode::InvalidCondition, error);
    }
    return lib::Result::fail(
        lib::ErrorCode::DbError,
        error.isEmpty() ? QStringLiteral("操作日志操作失败") : error);
}

QVariantMap logToMap(const OperationLogInfo &item)
{
    QVariantMap value;
    value.insert(QStringLiteral("log_id"), item.id);
    value.insert(QStringLiteral("operator_user_id"), item.operatorUserId);
    value.insert(QStringLiteral("operator_name"), item.operatorName);
    value.insert(QStringLiteral("action_type"), item.actionType);
    value.insert(QStringLiteral("target_type"), item.targetType);
    value.insert(QStringLiteral("target_id"), item.targetId);
    value.insert(QStringLiteral("detail"), item.detail);
    value.insert(QStringLiteral("created_at"), item.createdAt);
    return value;
}

QDateTime dateTimeFilter(const QVariant &value)
{
    if (value.canConvert<QDateTime>()) {
        const QDateTime dateTime = value.toDateTime();
        if (dateTime.isValid()) {
            return dateTime;
        }
    }
    return QDateTime::fromString(value.toString(), Qt::ISODate);
}

}

lib::Result OperationLogService::record(
    const lib::OperatorContext &context,
    const QString &actionType,
    const QString &targetType,
    const QString &targetId,
    const QString &detail)
{
    const lib::Result sessionCheck = PermissionService::requirePermission(
        context, Permission::ViewBooks);
    if (!sessionCheck.ok()) {
        return sessionCheck;
    }
    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    if (!record(session, actionType, targetType, targetId, detail, &error)) {
        return logFailure(error);
    }
    QVariantMap data;
    data.insert(QStringLiteral("action_type"), actionType.trimmed());
    data.insert(QStringLiteral("target_type"), targetType.trimmed());
    data.insert(QStringLiteral("target_id"), targetId.trimmed());
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("操作日志已记录"),
                       data,
                       {},
                       1};
}

lib::Result OperationLogService::listLogs(
    const lib::OperatorContext &context,
    const QVariantMap &filter,
    int page,
    int pageSize) const
{
    if (!lib::pageParamsValid(page, pageSize)) {
        return lib::Result::fail(lib::ErrorCode::InvalidPage,
                                 QStringLiteral("分页参数非法"));
    }

    OperationLogQuery query;
    query.actionType = filter.value(QStringLiteral("action_type"),
                                    filter.value(QStringLiteral("actionType")))
                           .toString();
    if (filter.contains(QStringLiteral("from"))) {
        query.from = dateTimeFilter(filter.value(QStringLiteral("from")));
    }
    if (filter.contains(QStringLiteral("to"))) {
        query.to = dateTimeFilter(filter.value(QStringLiteral("to")));
    }
    query.page = page;
    query.pageSize = pageSize;

    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    const OperationLogPage result = listLogs(session, query, &error);
    if (!error.isEmpty()) {
        return logFailure(error);
    }

    QVariantList list;
    for (const OperationLogInfo &item : result.items) {
        list.append(logToMap(item));
    }
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("查询操作日志成功"),
                       {},
                       list,
                       result.total};
}

bool OperationLogService::record(
    const UserSession &session,
    const QString &actionType,
    const QString &targetType,
    const QString &targetId,
    const QString &detail,
    QString *error)
{
    if (!session.isAuthenticated()) {
        if (error) {
            *error = QStringLiteral("请先登录");
        }
        return false;
    }

    const QString normalizedAction = actionType.trimmed();
    const QString normalizedTarget = targetType.trimmed();

    if (normalizedAction.isEmpty()
        || normalizedTarget.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "操作类型和操作对象类型不能为空");
        }
        return false;
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "INSERT INTO operation_logs ("
        "operator_user_id, operator_name, action_type, target_type, "
        "target_id, detail) "
        "VALUES (?, ?, ?, ?, ?, ?)"));

    query.addBindValue(session.userId);
    query.addBindValue(
        session.displayName.trimmed().isEmpty()
            ? session.username
            : session.displayName);
    query.addBindValue(normalizedAction);
    query.addBindValue(normalizedTarget);
    query.addBindValue(targetId.trimmed());
    query.addBindValue(detail.trimmed());

    if (!query.exec()) {
        qWarning().noquote()
        << QStringLiteral("写入操作日志失败：%1")
                .arg(query.lastError().text());

        if (error) {
            *error = QStringLiteral("写入操作日志失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }

    return true;
}

OperationLogPage OperationLogService::listLogs(
    const UserSession &session,
    const OperationLogQuery &criteria,
    QString *error) const
{
    OperationLogPage result;
    result.page = criteria.page > 0 ? criteria.page : 1;
    result.pageSize = qBound(1, criteria.pageSize, 200);

    if (!PermissionService::requirePermission(
            session,
            Permission::ViewOperationLogs,
            error)) {
        return result;
    }

    if (criteria.from.isValid()
        && criteria.to.isValid()
        && criteria.from > criteria.to) {
        if (error) {
            *error = QStringLiteral(
                "开始时间不能晚于结束时间");
        }
        return result;
    }

    QString where = QStringLiteral(" WHERE 1 = 1");
    QVariantList bindings;

    if (!criteria.actionType.trimmed().isEmpty()) {
        where += QStringLiteral(" AND ol.action_type = ?");
        bindings.append(criteria.actionType.trimmed());
    }

    if (criteria.from.isValid()) {
        where += QStringLiteral(" AND ol.created_at >= ?");
        bindings.append(
            criteria.from.toString(
                QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }

    if (criteria.to.isValid()) {
        where += QStringLiteral(" AND ol.created_at <= ?");
        bindings.append(
            criteria.to.toString(
                QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    }

    QSqlQuery countQuery(Database::connection());
    countQuery.prepare(
        QStringLiteral(
            "SELECT COUNT(*) FROM operation_logs ol")
        + where);

    for (const QVariant &value : bindings) {
        countQuery.addBindValue(value);
    }

    if (!countQuery.exec() || !countQuery.next()) {
        qWarning().noquote()
        << QStringLiteral("统计操作日志失败：%1")
                .arg(countQuery.lastError().text());

        if (error) {
            *error = QStringLiteral("查询操作日志失败");
        }
        return result;
    }

    result.total = countQuery.value(0).toInt();

    QSqlQuery query(Database::connection());
    query.prepare(
        QStringLiteral(
            "SELECT "
            "ol.id, "
            "COALESCE(ol.operator_user_id, 0), "
            "COALESCE(ol.operator_name, u.display_name, u.username, '系统'), "
            "ol.action_type, "
            "ol.target_type, "
            "COALESCE(ol.target_id, ''), "
            "COALESCE(ol.detail, ''), "
            "ol.created_at "
            "FROM operation_logs ol "
            "LEFT JOIN users u "
            "ON u.id = ol.operator_user_id")
        + where
        + QStringLiteral(
            " ORDER BY ol.created_at DESC, ol.id DESC "
            "LIMIT ? OFFSET ?"));

    for (const QVariant &value : bindings) {
        query.addBindValue(value);
    }

    query.addBindValue(result.pageSize);
    query.addBindValue(
        (result.page - 1) * result.pageSize);

    if (!query.exec()) {
        qWarning().noquote()
        << QStringLiteral("查询操作日志失败：%1")
                .arg(query.lastError().text());

        if (error) {
            *error = QStringLiteral("查询操作日志失败");
        }
        return result;
    }

    while (query.next()) {
        OperationLogInfo item;
        item.id = query.value(0).toLongLong();
        item.operatorUserId =
            query.value(1).toLongLong();
        item.operatorName = query.value(2).toString();
        item.actionType = query.value(3).toString();
        item.targetType = query.value(4).toString();
        item.targetId = query.value(5).toString();
        item.detail = query.value(6).toString();
        item.createdAt = query.value(7).toDateTime();
        result.items.append(item);
    }

    if (error) {
        error->clear();
    }

    return result;
}
