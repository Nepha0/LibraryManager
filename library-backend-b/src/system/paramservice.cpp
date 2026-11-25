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

lib::Result paramFailure(const QString &error)
{
    if (error == QStringLiteral("请先登录")
        || error == QStringLiteral("登录状态已失效，请重新登录")) {
        return lib::Result::fail(lib::ErrorCode::NotAuthenticated, error);
    }
    if (error == QStringLiteral("无权限")) {
        return lib::Result::fail(lib::ErrorCode::NoPermission, error);
    }
    if (error.contains(QStringLiteral("系统参数不存在"))) {
        return lib::failB(lib::ErrorCodeB::ParamNotFound, error);
    }
    if (error.contains(QStringLiteral("参数值"))
        || error.contains(QStringLiteral("有效整数"))) {
        return lib::failB(lib::ErrorCodeB::InvalidParamValue, error);
    }
    return lib::Result::fail(
        lib::ErrorCode::DbError,
        error.isEmpty() ? QStringLiteral("系统参数操作失败") : error);
}

}

lib::Result ParamService::getParam(
    const lib::OperatorContext &context,
    const QString &key) const
{
    const lib::Result sessionCheck = PermissionService::requirePermission(
        context, Permission::ViewBooks);
    if (!sessionCheck.ok()) {
        return sessionCheck;
    }
    QString error;
    const auto value = getParam(key, &error);
    if (!value.has_value()) {
        return paramFailure(error);
    }
    QVariantMap data;
    data.insert(QStringLiteral("key"), key.trimmed());
    data.insert(QStringLiteral("value"), *value);
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("读取系统参数成功"),
                       data,
                       {},
                       1};
}

lib::Result ParamService::getIntParam(
    const lib::OperatorContext &context,
    const QString &key) const
{
    const lib::Result sessionCheck = PermissionService::requirePermission(
        context, Permission::ViewBooks);
    if (!sessionCheck.ok()) {
        return sessionCheck;
    }
    QString error;
    const auto value = getIntParam(key, &error);
    if (!value.has_value()) {
        return paramFailure(error);
    }
    QVariantMap data;
    data.insert(QStringLiteral("key"), key.trimmed());
    data.insert(QStringLiteral("value"), *value);
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("读取系统参数成功"),
                       data,
                       {},
                       1};
}

lib::Result ParamService::setParam(
    const lib::OperatorContext &context,
    const QString &key,
    const QString &value)
{
    QString error;
    const UserSession session = UserSession::fromOperatorContext(context);
    if (!setParam(session, key, value, &error)) {
        return paramFailure(error);
    }
    QVariantMap data;
    data.insert(QStringLiteral("key"), key.trimmed());
    data.insert(QStringLiteral("value"), value.trimmed());
    return lib::Result{lib::ErrorCode::Success,
                       QStringLiteral("系统参数已更新"),
                       data,
                       {},
                       1};
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
