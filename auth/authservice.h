#pragma once
#include "permissionservice.h"
#include <QString>
#include <optional>

struct UserSession
{
    int userId = 0;
    QString username;
    QString displayName;
    QString role;
    QVector<Permission> permissions;

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
    }
};

class AuthService
{
public:
    static std::optional<UserSession> login(const QString &username,
                                            const QString &password,
                                            QString *error);

    static void logout(UserSession &session);
};