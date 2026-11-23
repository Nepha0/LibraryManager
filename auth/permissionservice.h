#pragma once

#include <QString>
#include <QtGlobal>
#include <QVector>

struct UserSession;

enum class Permission
{
    ViewBooks,
    ManageBooks,
    ViewReaders,
    ManageReaders,
    ManageCirculation,
    ManageUsers,
    ManageParams,
    ViewOperationLogs,
    ViewPersonalCenter
};

class PermissionService
{
public:
    static QVector<Permission> permissionsForRole(
        const QString &role);

    static bool hasPermission(const UserSession &session,
                              Permission permission);

    static bool requirePermission(const UserSession &session,
                                  Permission permission,
                                  QString *error);

    static quint64 currentSessionGeneration(
        int userId);

    static void invalidateUserSessions(
        int userId);
};