#pragma once

#include "common/contract.h"
#include <QDateTime>
#include <QString>
#include <QVector>
#include <optional>

struct UserSession;

struct UserInfo
{
    qlonglong id = 0;
    QString username;
    QString displayName;
    QString role;
    bool enabled = true;
    QDateTime createdAt;
};

struct UserQuery
{
    QString keyword;
    QString role;
    std::optional<bool> enabled;
    int page = 1;
    int pageSize = 20;
};

struct UserPage
{
    QVector<UserInfo> items;
    int total = 0;
    int page = 1;
    int pageSize = 20;
};

class UserService
{
public:
    // 统一契约入口。fields 支持 username、display_name、password、role。
    lib::Result addUser(const lib::OperatorContext &context,
                        const QVariantMap &fields);

    lib::Result updateUser(const lib::OperatorContext &context,
                           qint64 userId,
                           const QVariantMap &fields);

    lib::Result resetPassword(const lib::OperatorContext &context,
                              qint64 userId,
                              const QString &newPassword);

    lib::Result assignRole(const lib::OperatorContext &context,
                           qint64 userId,
                           const QString &role);

    lib::Result setUserStatus(const lib::OperatorContext &context,
                              qint64 userId,
                              bool enabled);

    lib::Result getUser(const lib::OperatorContext &context,
                        qint64 userId) const;

    lib::Result listUsers(const lib::OperatorContext &context,
                          const QVariantMap &filter,
                          int page = 1,
                          int pageSize = 20) const;

private:
    bool addUser(const UserSession &session,
                 const QString &username,
                 const QString &displayName,
                 const QString &password,
                 const QString &role,
                 QString *error);

    bool updateUser(const UserSession &session,
                    qlonglong userId,
                    const QString &username,
                    const QString &displayName,
                    QString *error);

    bool resetPassword(const UserSession &session,
                       qlonglong userId,
                       const QString &newPassword,
                       QString *error);

    bool assignRole(const UserSession &session,
                    qlonglong userId,
                    const QString &role,
                    QString *error);

    bool setUserStatus(const UserSession &session,
                       qlonglong userId,
                       bool enabled,
                       QString *error);

    std::optional<UserInfo> getUser(
        const UserSession &session,
        qlonglong userId,
        QString *error) const;

    UserPage listUsers(const UserSession &session,
                       const UserQuery &query,
                       QString *error) const;
};
