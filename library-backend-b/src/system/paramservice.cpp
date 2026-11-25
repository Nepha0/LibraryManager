#include "paramservice.h"

#include "../auth/authservice.h"
#include "../auth/permissionservice.h"
#include "common/database.h"
#include "operationlogservice.h"

#include <QDebug>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QSqlDatabase>

namespace {

const QSet<QString> &supportedParamKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("borrow_period_days"),
        QStringLiteral("borrow_limit"),
        QStringLiteral("max_renew_times"),
        QStringLiteral("reservation_hold_days"),
        QStringLiteral("renew_period_days")
    };
    return keys;
}

}

std::optional<QString> ParamService::getParam(const QString &key,
                                              QString *error) const
{
    const QString normalizedKey = key.trimmed();

    if (normalizedKey.isEmpty()) {
        if (error) {
            *error = QStringLiteral("参数名不能为空");
        }
        return std::nullopt;
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT param_value "
        "FROM system_params "
        "WHERE param_key = ?"));
    query.addBindValue(normalizedKey);

    if (!query.exec()) {
        qWarning().noquote()
        << QStringLiteral("读取系统参数失败：%1")
                .arg(query.lastError().text());

        if (error) {
            *error = QStringLiteral("读取系统参数失败");
        }
        return std::nullopt;
    }

    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("系统参数不存在：%1")
                         .arg(normalizedKey);
        }
        return std::nullopt;
    }

    if (error) {
        error->clear();
    }

    return query.value(0).toString();
}

std::optional<int> ParamService::getIntParam(const QString &key,
                                             QString *error) const
{
    const auto value = getParam(key, error);
    if (!value.has_value()) {
        return std::nullopt;
    }

    bool ok = false;
    const int number = value->toInt(&ok);

    if (!ok) {
        if (error) {
            *error = QStringLiteral("系统参数不是有效整数：%1")
                         .arg(key);
        }
        return std::nullopt;
    }

    return number;
}

bool ParamService::setParam(const UserSession &session,
                            const QString &key,
                            const QString &value,
                            QString *error)
{
    if (!PermissionService::requirePermission(
            session, Permission::ManageParams, error)) {
        return false;
    }

    const QString normalizedKey = key.trimmed();
    const QString normalizedValue = value.trimmed();

    if (!supportedParamKeys().contains(normalizedKey)) {
        if (error) {
            *error = QStringLiteral("不支持的系统参数：%1")
                         .arg(normalizedKey);
        }
        return false;
    }

    bool numberOk = false;
    const int number = normalizedValue.toInt(&numberOk);
    const bool allowZero =
        normalizedKey == QStringLiteral("max_renew_times");

    if (!numberOk || number < 0
        || (!allowZero && number == 0)) {
        if (error) {
            *error = allowZero
                         ? QStringLiteral(
                               "参数值必须是非负整数")
                         : QStringLiteral(
                               "参数值必须是正整数");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();

    if (!database.transaction()) {
        qWarning().noquote()
        << QStringLiteral("启动参数修改事务失败：%1")
                .arg(database.lastError().text());

        if (error) {
            *error = QStringLiteral("修改系统参数失败");
        }
        return false;
    }

    QSqlQuery currentQuery(database);
    currentQuery.prepare(QStringLiteral(
        "SELECT param_value "
        "FROM system_params "
        "WHERE param_key = ?"));
    currentQuery.addBindValue(normalizedKey);

    if (!currentQuery.exec()) {
        database.rollback();

        qWarning().noquote()
            << QStringLiteral("检查系统参数失败：%1")
                   .arg(currentQuery.lastError().text());

        if (error) {
            *error = QStringLiteral("修改系统参数失败");
        }
        return false;
    }

    if (!currentQuery.next()) {
        database.rollback();

        if (error) {
            *error = QStringLiteral("系统参数不存在：%1")
                         .arg(normalizedKey);
        }
        return false;
    }

    const QString previousValue =
        currentQuery.value(0).toString();
    const QString newValue = QString::number(number);

    QSqlQuery updateQuery(database);
    updateQuery.prepare(QStringLiteral(
        "UPDATE system_params "
        "SET param_value = ? "
        "WHERE param_key = ?"));
    updateQuery.addBindValue(newValue);
    updateQuery.addBindValue(normalizedKey);

    if (!updateQuery.exec()) {
        database.rollback();

        qWarning().noquote()
            << QStringLiteral("修改系统参数失败：%1")
                   .arg(updateQuery.lastError().text());

        if (error) {
            *error = QStringLiteral("修改系统参数失败");
        }
        return false;
    }

    OperationLogService logService;
    QString logError;

    const bool logged = logService.record(
        session,
        QStringLiteral("PARAM_UPDATE"),
        QStringLiteral("system_param"),
        normalizedKey,
        QStringLiteral("系统参数 %1：%2 -> %3")
            .arg(normalizedKey, previousValue, newValue),
        &logError);

    if (!logged) {
        database.rollback();

        qWarning().noquote()
            << QStringLiteral("记录参数修改日志失败：%1")
                   .arg(logError);

        if (error) {
            *error = QStringLiteral(
                "修改系统参数失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        database.rollback();

        qWarning().noquote()
            << QStringLiteral("提交参数修改事务失败：%1")
                   .arg(database.lastError().text());

        if (error) {
            *error = QStringLiteral("修改系统参数失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }

    return true;
}
