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

class ReaderService
{
public:
    bool addReader(const UserSession &session,
                   const QString &readerNo,
                   const QString &name,
                   const QString &phone,
                   QString *error);
    QVector<ReaderInfo> readers(QString *error) const;
};
