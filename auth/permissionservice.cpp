#include "permissionservice.h"

#include "authservice.h"
#include "../common/database.h"

#include <QSqlQuery>
#include <QVariant>
#include <QHash>

namespace {

QHash<int, quint64> &sessionGenerations()
{
    static QHash<int, quint64> generations;
    return generations;
}

}

QVector<Permission> PermissionService::permissionsForRole(
    const QString &role)
{
    const QString normalizedRole = role.trimmed().toLower();

    if (normalizedRole == QStringLiteral("admin")) {
        return {
            Permission::ViewBooks,
            Permission::ManageBooks,
            Permission::ViewReaders,
            Permission::ManageReaders,
            Permission::ManageCirculation,
            Permission::ManageUsers,
            Permission::ManageParams,
            Permission::ViewOperationLogs,
            Permission::ViewPersonalCenter
        };
    }

    if (normalizedRole == QStringLiteral("librarian")) {
        return {
            Permission::ViewBooks,
            Permission::ManageBooks,
            Permission::ViewReaders,
            Permission::ManageReaders,
            Permission::ManageCirculation
        };
    }

    if (normalizedRole == QStringLiteral("reader")) {
        return {
            Permission::ViewBooks,
            Permission::ViewPersonalCenter
        };
    }

    return {};
}

bool PermissionService::hasPermission(
    const UserSession &session,
    Permission permission)
{
    if (!session.isAuthenticated()) {
        return false;
    }

    return session.permissions.contains(permission);
}

bool PermissionService::requirePermission(
    const UserSession &session,
    Permission permission,
    QString *error)
{
    if (!session.isAuthenticated()) {
        if (error) {
            *error = QStringLiteral("请先登录");
        }
        return false;
    }

    if (session.generation
        != currentSessionGeneration(
            session.userId)) {
        if (error) {
            *error = QStringLiteral(
                "登录状态已失效，请重新登录");
        }
        return false;
    }

    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT username, role, active "
        "FROM users WHERE id = ?"));
    query.addBindValue(session.userId);

    if (!query.exec()) {
        if (error) {
            *error = QStringLiteral(
                "验证登录状态失败");
        }
        return false;
    }

    if (!query.next()
        || query.value(2).toInt() != 1
        || query.value(0).toString()
               != session.username
        || query.value(1).toString()
               != session.role) {
        if (error) {
            *error = QStringLiteral(
                "登录状态已失效，请重新登录");
        }
        return false;
    }

    const QVector<Permission> currentPermissions =
        permissionsForRole(
            query.value(1).toString());

    if (!currentPermissions.contains(permission)) {
        if (error) {
            *error = QStringLiteral("无权限");
        }
        return false;
    }

    if (error) {
        error->clear();
    }

    return true;
}

quint64 PermissionService::currentSessionGeneration(
    int userId)
{
    return sessionGenerations().value(
        userId,
        0);
}

void PermissionService::invalidateUserSessions(
    int userId)
{
    QHash<int, quint64> &generations =
        sessionGenerations();

    generations.insert(
        userId,
        generations.value(userId, 0) + 1);
}