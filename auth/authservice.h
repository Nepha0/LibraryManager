#pragma once

#include <QString>
#include <optional>

struct UserSession
{
    int userId = 0;
    QString username;
    QString displayName;
    QString role;

    bool isAdmin() const
    {
        return role == QStringLiteral("admin");
    }
};

class AuthService
{
public:
    static std::optional<UserSession> login(const QString &username,
                                            const QString &password,
                                            QString *error);
};
