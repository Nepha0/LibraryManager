#include "permissionservice.h"

#include "authservice.h"

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

    const QVector<Permission> permissions =
        permissionsForRole(session.role);

    return permissions.contains(permission);
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

    if (!hasPermission(session, permission)) {
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