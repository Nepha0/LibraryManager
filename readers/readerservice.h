#pragma once

#include "../auth/authservice.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

struct ReaderInfo
{
    int id = 0;
    QString readerNo;
    QString name;
    QString phone;
    int activeLoans = 0;
    bool enabled = true;
};

struct ReaderEligibilityResult
{
    bool eligible = false;
    bool readerEnabled = false;
    bool accountEnabled = true;
    int activeLoans = 0;
    int borrowLimit = 0;
    QString reason;
    QStringList reasons;
};

struct ReaderQuery
{
    QString readerNo;
    QString name;
    std::optional<bool> enabled;
};

struct ReaderDetail
{
    ReaderInfo info;
    int totalLoans = 0;
    int returnedLoans = 0;
    int overdueLoans = 0;
};

class ReaderService
{
public:
    bool addReader(const UserSession &session,
                   const QString &readerNo,
                   const QString &name,
                   const QString &phone,
                   QString *error);

    bool updateReader(const UserSession &session,
                      int readerId,
                      const QString &readerNo,
                      const QString &name,
                      const QString &phone,
                      QString *error);

    bool setReaderStatus(const UserSession &session,
                         int readerId,
                         bool enabled,
                         QString *error);

    ReaderEligibilityResult checkReaderEligibility(
        const UserSession &session,
        int readerId,
        QString *error) const;

    std::optional<ReaderDetail> getReader(
        const UserSession &session,
        int readerId,
        QString *error) const;

    QVector<ReaderInfo> readers(
        const UserSession &session,
        const ReaderQuery &query,
        QString *error) const;

    QVector<ReaderInfo> readers(
        const UserSession &session,
        QString *error) const;
};
