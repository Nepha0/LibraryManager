#pragma once
#include "permissionservice.h"
#include "common/contract.h"
#include <QString>
#include <optional>

struct UserSession
{
    int userId = 0;
    QString username;
    QString displayName;
    QString role;
    QVector<Permission> permissions;
    quint64 generation = 0;

    bool isAuthenticated() const
    {
        return userId > 0;
    }

    bool isAdmin() const
    {
        return role == QStringLiteral("admin");
    }

    bool isLibrarian() const
    {
        return role == QStringLiteral("librarian");
    }

    bool isReader() const
    {
        return role == QStringLiteral("reader");
    }

    void clear()
    {
        userId = 0;
        username.clear();
        displayName.clear();
        role.clear();
        permissions.clear();
        generation = 0;
    }

    lib::OperatorContext operatorContext() const
    {
        lib::Role contextRole = lib::Role::Reader;
        if (isAdmin()) {
            contextRole = lib::Role::Admin;
        } else if (isLibrarian()) {
            contextRole = lib::Role::Librarian;
        }
        return {userId,
                username,
                contextRole,
                isAuthenticated(),
                static_cast<qint64>(generation)};
    }

    static UserSession fromOperatorContext(
        const lib::OperatorContext &context)
    {
        UserSession session;
        session.userId = context.authenticated
            ? static_cast<int>(context.userId)
            : 0;
        session.username = context.userName;
        session.role = context.role == lib::Role::Admin
            ? QStringLiteral("admin")
            : (context.role == lib::Role::Librarian
                   ? QStringLiteral("librarian")
                   : QStringLiteral("reader"));
        session.generation = static_cast<quint64>(
            context.session_generation);
        session.permissions = PermissionService::permissionsForRole(
            session.role);
        return session;
    }
};

class AuthService
{
public:
    // 统一契约入口：成功时 data 包含 user_id/user_name/role/
    // authenticated/session_generation；失败时使用 B 段错误码。
    static lib::Result login(const QString &username,
                             const QString &password);

private:
    static std::optional<UserSession> login(const QString &username,
                                            const QString &password,
                                            QString *error);

public:
    static lib::Result logout(lib::OperatorContext &context);
};
