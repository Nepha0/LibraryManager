#include "userservice.h"

#include "authservice.h"
#include "permissionservice.h"
#include "../common/database.h"
#include "../common/operationlogservice.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QVariantList>

namespace {

bool isSupportedRole(const QString &role)
{
    static const QSet<QString> roles = {
        QStringLiteral("admin"),
        QStringLiteral("librarian"),
        QStringLiteral("reader")
    };

    return roles.contains(role);
}

bool isDuplicateKeyError(const QSqlError &error)
{
    const QString message =
        error.nativeErrorCode()
        + QStringLiteral(" ")
        + error.text();

    return message.contains(QStringLiteral("1062"))
           || message.contains(
               QStringLiteral("23000"))
           || message.contains(
               QStringLiteral("Duplicate entry"),
               Qt::CaseInsensitive);
}

QString createSalt()
{
    return QUuid::createUuid().toString(
        QUuid::WithoutBraces);
}

QString hashPassword(const QString &salt,
                     const QString &password)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(
            (salt + password).toUtf8(),
            QCryptographicHash::Sha256)
            .toHex());
}

UserInfo userFromQuery(const QSqlQuery &query)
{
    UserInfo user;
    user.id = query.value(0).toLongLong();
    user.username = query.value(1).toString();
    user.displayName = query.value(2).toString();
    user.role = query.value(3).toString();
    user.enabled = query.value(4).toBool();
    user.createdAt = query.value(5).toDateTime();
    return user;
}

}

std::optional<UserInfo> UserService::getUser(
    const UserSession &session,
    qlonglong userId,
    QString *error) const
{
    if (!PermissionService::requirePermission(
            session,
            Permission::ManageUsers,
            error)) {
        return std::nullopt;
    }

    if (userId <= 0) {
        if (error) {
            *error = QStringLiteral("无效的用户ID");
        }
        return std::nullopt;
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT id, username, display_name, "
        "role, active, created_at "
        "FROM users "
        "WHERE id = ?"));
    query.addBindValue(userId);

    if (!query.exec()) {
        qWarning().noquote()
        << QStringLiteral("查询用户失败：%1")
                .arg(query.lastError().text());

        if (error) {
            *error = QStringLiteral("查询用户失败");
        }
        return std::nullopt;
    }

    if (!query.next()) {
        if (error) {
            *error = QStringLiteral("用户不存在");
        }
        return std::nullopt;
    }

    if (error) {
        error->clear();
    }

    return userFromQuery(query);
}

UserPage UserService::listUsers(
    const UserSession &session,
    const UserQuery &criteria,
    QString *error) const
{
    UserPage result;
    result.page =
        criteria.page > 0 ? criteria.page : 1;
    result.pageSize =
        qBound(1, criteria.pageSize, 200);

    if (!PermissionService::requirePermission(
            session,
            Permission::ManageUsers,
            error)) {
        return result;
    }

    const QString normalizedRole =
        criteria.role.trimmed().toLower();

    if (!normalizedRole.isEmpty()
        && !isSupportedRole(normalizedRole)) {
        if (error) {
            *error = QStringLiteral("不支持的角色");
        }
        return result;
    }

    QString where = QStringLiteral(" WHERE 1 = 1");
    QVariantList bindings;

    const QString keyword =
        criteria.keyword.trimmed();

    if (!keyword.isEmpty()) {
        where += QStringLiteral(
            " AND (username LIKE ? "
            "OR display_name LIKE ?)");
        const QString pattern =
            QStringLiteral("%")
            + keyword
            + QStringLiteral("%");
        bindings.append(pattern);
        bindings.append(pattern);
    }

    if (!normalizedRole.isEmpty()) {
        where += QStringLiteral(" AND role = ?");
        bindings.append(normalizedRole);
    }

    if (criteria.enabled.has_value()) {
        where += QStringLiteral(" AND active = ?");
        bindings.append(
            *criteria.enabled ? 1 : 0);
    }

    QSqlQuery countQuery(Database::connection());
    countQuery.prepare(
        QStringLiteral("SELECT COUNT(*) FROM users")
        + where);

    for (const QVariant &value : bindings) {
        countQuery.addBindValue(value);
    }

    if (!countQuery.exec() || !countQuery.next()) {
        qWarning().noquote()
        << QStringLiteral("统计用户失败：%1")
                .arg(countQuery.lastError().text());

        if (error) {
            *error = QStringLiteral("查询用户失败");
        }
        return result;
    }

    result.total = countQuery.value(0).toInt();

    QSqlQuery query(Database::connection());
    query.prepare(
        QStringLiteral(
            "SELECT id, username, display_name, "
            "role, active, created_at "
            "FROM users")
        + where
        + QStringLiteral(
            " ORDER BY id DESC "
            "LIMIT ? OFFSET ?"));

    for (const QVariant &value : bindings) {
        query.addBindValue(value);
    }

    query.addBindValue(result.pageSize);
    query.addBindValue(
        (result.page - 1) * result.pageSize);

    if (!query.exec()) {
        qWarning().noquote()
        << QStringLiteral("查询用户失败：%1")
                .arg(query.lastError().text());

        if (error) {
            *error = QStringLiteral("查询用户失败");
        }
        return result;
    }

    while (query.next()) {
        result.items.append(
            userFromQuery(query));
    }

    if (error) {
        error->clear();
    }

    return result;
}

bool UserService::addUser(
    const UserSession &session,
    const QString &username,
    const QString &displayName,
    const QString &password,
    const QString &role,
    QString *error)
{
    if (!PermissionService::requirePermission(
            session,
            Permission::ManageUsers,
            error)) {
        return false;
    }

    const QString normalizedUsername =
        username.trimmed();
    const QString normalizedDisplayName =
        displayName.trimmed();
    const QString normalizedRole =
        role.trimmed().toLower();

    if (normalizedUsername.isEmpty()
        || normalizedDisplayName.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "账号和姓名不能为空");
        }
        return false;
    }

    if (password.size() < 6) {
        if (error) {
            *error = QStringLiteral(
                "密码长度不能少于6位");
        }
        return false;
    }

    if (!isSupportedRole(normalizedRole)) {
        if (error) {
            *error = QStringLiteral("不支持的角色");
        }
        return false;
    }

    const QString salt = createSalt();
    const QString passwordHash =
        hashPassword(salt, password);

    QSqlDatabase database = Database::connection();

    if (!database.transaction()) {
        if (error) {
            *error = QStringLiteral("新增用户失败");
        }
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO users ("
        "username, display_name, password_hash, "
        "salt, role, active) "
        "VALUES (?, ?, ?, ?, ?, 1)"));
    query.addBindValue(normalizedUsername);
    query.addBindValue(normalizedDisplayName);
    query.addBindValue(passwordHash);
    query.addBindValue(salt);
    query.addBindValue(normalizedRole);

    if (!query.exec()) {
        database.rollback();

        if (error) {
            *error = isDuplicateKeyError(
                         query.lastError())
                         ? QStringLiteral("账号已存在")
                         : QStringLiteral("新增用户失败");
        }
        return false;
    }

    qlonglong userId =
        query.lastInsertId().toLongLong();

    if (userId <= 0) {
        QSqlQuery idQuery(database);
        if (idQuery.exec(QStringLiteral(
                "SELECT LAST_INSERT_ID()"))
            && idQuery.next()) {
            userId =
                idQuery.value(0).toLongLong();
        }
    }

    OperationLogService logService;
    QString logError;

    if (!logService.record(
            session,
            QStringLiteral("USER_CREATE"),
            QStringLiteral("user"),
            QString::number(userId),
            QStringLiteral("新增账号 %1，角色 %2")
                .arg(normalizedUsername,
                     normalizedRole),
            &logError)) {
        database.rollback();

        if (error) {
            *error = QStringLiteral(
                "新增用户失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        database.rollback();

        if (error) {
            *error = QStringLiteral("新增用户失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }

    return true;
}

bool UserService::updateUser(
    const UserSession &session,
    qlonglong userId,
    const QString &username,
    const QString &displayName,
    QString *error)
{
    if (!PermissionService::requirePermission(
            session,
            Permission::ManageUsers,
            error)) {
        return false;
    }

    if (userId <= 0) {
        if (error) {
            *error = QStringLiteral("无效的用户ID");
        }
        return false;
    }

    const QString normalizedUsername =
        username.trimmed();
    const QString normalizedDisplayName =
        displayName.trimmed();

    if (normalizedUsername.isEmpty()
        || normalizedDisplayName.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "账号和姓名不能为空");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();

    if (!database.transaction()) {
        if (error) {
            *error = QStringLiteral("修改用户失败");
        }
        return false;
    }

    QSqlQuery currentQuery(database);
    currentQuery.prepare(QStringLiteral(
        "SELECT username, display_name "
        "FROM users WHERE id = ?"));
    currentQuery.addBindValue(userId);

    if (!currentQuery.exec()
        || !currentQuery.next()) {
        database.rollback();

        if (error) {
            *error = currentQuery.lastError()
            .text()
                    .isEmpty()
                ? QStringLiteral("用户不存在")
                : QStringLiteral("修改用户失败");
        }
        return false;
    }

    const QString previousUsername =
        currentQuery.value(0).toString();
    const QString previousDisplayName =
        currentQuery.value(1).toString();

    QSqlQuery updateQuery(database);
    updateQuery.prepare(QStringLiteral(
        "UPDATE users "
        "SET username = ?, display_name = ? "
        "WHERE id = ?"));
    updateQuery.addBindValue(normalizedUsername);
    updateQuery.addBindValue(
        normalizedDisplayName);
    updateQuery.addBindValue(userId);

    if (!updateQuery.exec()) {
        database.rollback();

        if (error) {
            *error = isDuplicateKeyError(
                         updateQuery.lastError())
                         ? QStringLiteral("账号已存在")
                         : QStringLiteral("修改用户失败");
        }
        return false;
    }

    OperationLogService logService;
    QString logError;

    if (!logService.record(
            session,
            QStringLiteral("USER_UPDATE"),
            QStringLiteral("user"),
            QString::number(userId),
            QStringLiteral(
                "账号 %1 -> %2，姓名 %3 -> %4")
                .arg(previousUsername,
                     normalizedUsername,
                     previousDisplayName,
                     normalizedDisplayName),
            &logError)) {
        database.rollback();

        if (error) {
            *error = QStringLiteral(
                "修改用户失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        database.rollback();

        if (error) {
            *error = QStringLiteral("修改用户失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }

    return true;
}

bool UserService::resetPassword(
    const UserSession &session,
    qlonglong userId,
    const QString &newPassword,
    QString *error)
{
    if (!PermissionService::requirePermission(
            session,
            Permission::ManageUsers,
            error)) {
        return false;
    }

    if (userId <= 0) {
        if (error) {
            *error = QStringLiteral("无效的用户ID");
        }
        return false;
    }

    if (newPassword.size() < 6) {
        if (error) {
            *error = QStringLiteral(
                "密码长度不能少于6位");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();

    if (!database.transaction()) {
        if (error) {
            *error = QStringLiteral("重置密码失败");
        }
        return false;
    }

    QSqlQuery userQuery(database);
    userQuery.prepare(QStringLiteral(
        "SELECT username FROM users WHERE id = ?"));
    userQuery.addBindValue(userId);

    if (!userQuery.exec() || !userQuery.next()) {
        database.rollback();

        if (error) {
            *error = userQuery.lastError()
            .text()
                    .isEmpty()
                ? QStringLiteral("用户不存在")
                : QStringLiteral("重置密码失败");
        }
        return false;
    }

    const QString username =
        userQuery.value(0).toString();
    const QString salt = createSalt();
    const QString passwordHash =
        hashPassword(salt, newPassword);

    QSqlQuery updateQuery(database);
    updateQuery.prepare(QStringLiteral(
        "UPDATE users "
        "SET password_hash = ?, salt = ? "
        "WHERE id = ?"));
    updateQuery.addBindValue(passwordHash);
    updateQuery.addBindValue(salt);
    updateQuery.addBindValue(userId);

    if (!updateQuery.exec()) {
        database.rollback();

        if (error) {
            *error = QStringLiteral("重置密码失败");
        }
        return false;
    }

    OperationLogService logService;
    QString logError;

    if (!logService.record(
            session,
            QStringLiteral("USER_PASSWORD_RESET"),
            QStringLiteral("user"),
            QString::number(userId),
            QStringLiteral("重置账号 %1 的密码")
                .arg(username),
            &logError)) {
        database.rollback();

        if (error) {
            *error = QStringLiteral(
                "重置密码失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        database.rollback();

        if (error) {
            *error = QStringLiteral("重置密码失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }

    return true;
}

bool UserService::assignRole(
    const UserSession &session,
    qlonglong userId,
    const QString &role,
    QString *error)
{
    if (!PermissionService::requirePermission(
            session,
            Permission::ManageUsers,
            error)) {
        return false;
    }

    if (userId <= 0) {
        if (error) {
            *error = QStringLiteral("无效的用户ID");
        }
        return false;
    }

    const QString normalizedRole =
        role.trimmed().toLower();

    if (!isSupportedRole(normalizedRole)) {
        if (error) {
            *error = QStringLiteral("不支持的角色");
        }
        return false;
    }

    if (session.userId == userId
        && normalizedRole
               != QStringLiteral("admin")) {
        if (error) {
            *error = QStringLiteral(
                "不能取消当前登录账号的管理员角色");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();

    if (!database.transaction()) {
        if (error) {
            *error = QStringLiteral("分配角色失败");
        }
        return false;
    }

    QSqlQuery userQuery(database);
    userQuery.prepare(QStringLiteral(
        "SELECT username, role "
        "FROM users WHERE id = ?"));
    userQuery.addBindValue(userId);

    if (!userQuery.exec() || !userQuery.next()) {
        database.rollback();

        if (error) {
            *error = userQuery.lastError()
            .text()
                    .isEmpty()
                ? QStringLiteral("用户不存在")
                : QStringLiteral("分配角色失败");
        }
        return false;
    }

    const QString username =
        userQuery.value(0).toString();
    const QString previousRole =
        userQuery.value(1).toString();

    QSqlQuery updateQuery(database);
    updateQuery.prepare(QStringLiteral(
        "UPDATE users SET role = ? WHERE id = ?"));
    updateQuery.addBindValue(normalizedRole);
    updateQuery.addBindValue(userId);

    if (!updateQuery.exec()) {
        database.rollback();

        if (error) {
            *error = QStringLiteral("分配角色失败");
        }
        return false;
    }

    OperationLogService logService;
    QString logError;

    if (!logService.record(
            session,
            QStringLiteral("USER_ROLE_ASSIGN"),
            QStringLiteral("user"),
            QString::number(userId),
            QStringLiteral("账号 %1：角色 %2 -> %3")
                .arg(username,
                     previousRole,
                     normalizedRole),
            &logError)) {
        database.rollback();

        if (error) {
            *error = QStringLiteral(
                "分配角色失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        database.rollback();

        if (error) {
            *error = QStringLiteral("分配角色失败");
        }
        return false;
    }

    if (error) {
        error->clear();
    }

    return true;
}