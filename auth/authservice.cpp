#include "authservice.h"

#include "../common/database.h"

#include <QCryptographicHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

std::optional<UserSession> AuthService::login(const QString &username,
                                              const QString &password,
                                              QString *error)
{
    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT id, username, display_name, password_hash, salt, role, active "
        "FROM users WHERE username = ?"));
    query.addBindValue(username.trimmed());
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return std::nullopt;
    }
    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("账号或密码不正确");
        }
        return std::nullopt;
    }
    if (query.value(6).toInt() != 1) {
        if (error) {
            *error = QStringLiteral("该账号已停用，请联系管理员");
        }
        return std::nullopt;
    }

    const QString salt = query.value(4).toString();
    const QString actualHash =
        QString::fromLatin1(QCryptographicHash::hash(
                                (salt + password).toUtf8(),
                                QCryptographicHash::Sha256)
                                .toHex());
    if (actualHash != query.value(3).toString()) {
        if (error) {
            *error = QStringLiteral("账号或密码不正确");
        }
        return std::nullopt;
    }

    UserSession session;
    session.userId = query.value(0).toInt();
    session.username = query.value(1).toString();
    session.displayName = query.value(2).toString();
    session.role = query.value(5).toString();
    return session;
}
