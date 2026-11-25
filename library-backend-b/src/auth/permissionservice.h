#pragma once

#include "common/contract.h"
#include <QString>
#include <QtGlobal>
#include <QVector>

struct UserSession;
class UserService;
class ReaderService;
class ParamService;
class OperationLogService;
class BookService;
class CirculationService;
class StatisticsService;

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

    static bool hasPermission(const lib::OperatorContext &context,
                              Permission permission);

    static lib::Result requirePermission(
        const lib::OperatorContext &context,
        Permission permission);

    static quint64 currentSessionGeneration(
        int userId);

    static void invalidateUserSessions(
        int userId);

private:
    friend class UserService;
    friend class ReaderService;
    friend class ParamService;
    friend class OperationLogService;
    friend class BookService;
    friend class CirculationService;
    friend class StatisticsService;

    static bool hasPermission(const UserSession &session,
                              Permission permission);

    static bool requirePermission(const UserSession &session,
                                  Permission permission,
                                  QString *error);
};
