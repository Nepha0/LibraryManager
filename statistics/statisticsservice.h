#pragma once

#include "../library-backend-b/src/auth/authservice.h"

#include <QDateTime>
#include <QString>
#include <QVector>

struct DashboardStats
{
    int titles = 0;
    int copies = 0;
    int availableCopies = 0;
    int activeLoans = 0;
    int overdueLoans = 0;
    int readers = 0;
};

struct LoanHistoryInfo
{
    QString readerNo;
    QString readerName;
    QString copyCode;
    QString title;
    QDateTime borrowedAt;
    QDateTime dueAt;
    QDateTime returnedAt;
};

class StatisticsService
{
public:
    DashboardStats dashboard(
        const UserSession &session,
        QString *error) const;

    QVector<LoanHistoryInfo> loanHistory(
        const UserSession &session,
        int limit,
        QString *error) const;

    bool exportCsv(
        const UserSession &session,
        const QString &fileName,
        QString *error) const;
};
