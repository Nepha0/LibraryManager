#include "paramservice.h"

#include "../auth/authservice.h"
#include "../auth/permissionservice.h"
#include "database.h"

#include <QDebug>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

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

    if (!numberOk || number < 0 || (!allowZero && number == 0)) {
        if (error) {
            *error = allowZero
                         ? QStringLiteral("参数值必须是非负整数")
                         : QStringLiteral("参数值必须是正整数");
        }
        return false;
    }

    QSqlQuery existsQuery(Database::connection());
    existsQuery.prepare(QStringLiteral(
        "SELECT 1 FROM system_params WHERE param_key = ?"));
    existsQuery.addBindValue(normalizedKey);

    if (!existsQuery.exec()) {
        qWarning().noquote()
        << QStringLiteral("检查系统参数失败：%1")
                .arg(existsQuery.lastError().text());

        if (error) {
            *error = QStringLiteral("修改系统参数失败");
        }
        return false;
    }

    if (!existsQuery.next()) {
        if (error) {
            *error = QStringLiteral("系统参数不存在：%1")
                         .arg(normalizedKey);
        }
        return false;
    }

    QSqlQuery updateQuery(Database::connection());
    updateQuery.prepare(QStringLiteral(
        "UPDATE system_params "
        "SET param_value = ? "
        "WHERE param_key = ?"));
    updateQuery.addBindValue(QString::number(number));
    updateQuery.addBindValue(normalizedKey);

    if (!updateQuery.exec()) {
        qWarning().noquote()
        << QStringLiteral("修改系统参数失败：%1")
                .arg(updateQuery.lastError().text());

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