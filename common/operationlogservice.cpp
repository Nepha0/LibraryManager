#include "operationlogservice.h"

#include "../auth/authservice.h"
#include "../auth/permissionservice.h"
#include "database.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QVariantList>

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
        "operator_user_id, action_type, target_type, "
        "target_id, detail) "
        "VALUES (?, ?, ?, ?, ?)"));

    query.addBindValue(session.userId);
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
            "COALESCE(u.display_name, u.username, '系统'), "
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