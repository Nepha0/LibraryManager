#include "authservice.h"

#include "../common/database.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

std::optional<UserSession> AuthService::login(const QString &username,
                                              const QString &password,
                                              QString *error)
{
    const QString normalizedUsername = username.trimmed();

    if (normalizedUsername.isEmpty() || password.isEmpty()) {
        if (error) {
            *error = QStringLiteral("账号或密码错误");
        }
        return std::nullopt;
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT id, username, display_name, password_hash, "
        "salt, role, active "
        "FROM users "
        "WHERE username = ?"));
    query.addBindValue(normalizedUsername);

    if (!query.exec()) {
        qWarning().noquote()
            << QStringLiteral("登录查询失败：%1")
                   .arg(query.lastError().text());

        if (error) {
            *error = QStringLiteral("系统异常，请稍后重试");
        }
        return std::nullopt;
    }

    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("账号或密码错误");
        }
        return std::nullopt;
    }

    if (!query.value(6).toBool()) {
        if (error) {
            *error = QStringLiteral("账号已停用，请联系管理员");
        }
        return std::nullopt;
    }

    const QString salt = query.value(4).toString();
    const QString actualHash =
        QString::fromLatin1(
            QCryptographicHash::hash(
                (salt + password).toUtf8(),
                QCryptographicHash::Sha256)
                .toHex());

    const QString storedHash = query.value(3).toString();
    if (actualHash != storedHash) {
        if (error) {
            *error = QStringLiteral("账号或密码错误");
        }
        return std::nullopt;
    }

    UserSession session;
    session.userId = query.value(0).toInt();
    session.username = query.value(1).toString();
    session.displayName = query.value(2).toString();
    session.role = query.value(5).toString();
    session.permissions = PermissionService::permissionsForRole(session.role);

    if (error) {
        error->clear();
    }

    return session;
}

void AuthService::logout(UserSession &session)
{
    session.clear();
}