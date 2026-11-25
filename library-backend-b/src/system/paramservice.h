#pragma once

#include <QString>
#include <optional>

struct UserSession;

class ParamService
{
public:
    std::optional<QString> getParam(const QString &key,
                                    QString *error) const;

    std::optional<int> getIntParam(const QString &key,
                                   QString *error) const;

    bool setParam(const UserSession &session,
                  const QString &key,
                  const QString &value,
                  QString *error);
};