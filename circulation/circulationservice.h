#pragma once

#include "../auth/authservice.h"

#include <QDateTime>
#include <QString>
#include <QVector>

struct ActiveLoanInfo
{
    int loanId = 0;
    QString readerNo;
    QString readerName;
    QString copyCode;
    QString title;
    QDateTime borrowedAt;
    QDateTime dueAt;
    bool overdue = false;
};

class CirculationService
{
public:
    bool borrowBook(const UserSession &session,
                    const QString &readerNo,
                    int copyId,
                    QString *error);
    bool returnBook(const UserSession &session,
                    int loanId,
                    QString *error);
    QVector<ActiveLoanInfo> activeLoans(QString *error) const;
};
