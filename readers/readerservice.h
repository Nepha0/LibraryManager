#pragma once

#include "../auth/authservice.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct ReaderInfo
{
    int id = 0;
    QString readerNo;
    QString name;
    QString phone;
    QString type;
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
                   const QString &type,
                   QString *error);
    QVector<ReaderInfo> readers(QString *error) const;
    QStringList readerTypes(QString *error) const;
};
