#pragma once

#include "common/contract.h"
#include <QString>
#include <optional>

struct UserSession;
class ReaderService;
class CirculationService;

class ParamService
{
public:
    lib::Result getParam(const lib::OperatorContext &context,
                         const QString &key) const;

    lib::Result getIntParam(const lib::OperatorContext &context,
                            const QString &key) const;

    lib::Result setParam(const lib::OperatorContext &context,
                         const QString &key,
                         const QString &value);

private:
    friend class ReaderService;
    friend class CirculationService;

    std::optional<QString> getParam(const QString &key,
                                    QString *error) const;

    std::optional<int> getIntParam(const QString &key,
                                   QString *error) const;

    bool setParam(const UserSession &session,
                  const QString &key,
                  const QString &value,
                  QString *error);
};
