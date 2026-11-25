#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

struct UserSession;

struct OperationLogInfo
{
    qlonglong id = 0;
    qlonglong operatorUserId = 0;
    QString operatorName;
    QString actionType;
    QString targetType;
    QString targetId;
    QString detail;
    QDateTime createdAt;
};

struct OperationLogQuery
{
    QString actionType;
    QDateTime from;
    QDateTime to;
    int page = 1;
    int pageSize = 50;
};

struct OperationLogPage
{
    QVector<OperationLogInfo> items;
    int total = 0;
    int page = 1;
    int pageSize = 50;
};

class OperationLogService
{
public:
    bool record(const UserSession &session,
                const QString &actionType,
                const QString &targetType,
                const QString &targetId,
                const QString &detail,
                QString *error);

    OperationLogPage listLogs(const UserSession &session,
                              const OperationLogQuery &query,
                              QString *error) const;
};