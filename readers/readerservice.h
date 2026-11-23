#pragma once

#include "../auth/authservice.h"

#include <QString>
#include <QVector>

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

    QVector<ReaderInfo> readers(
        const UserSession &session,
        QString *error) const;
};
