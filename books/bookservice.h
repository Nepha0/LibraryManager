#pragma once

#include "../auth/authservice.h"

#include <QString>
#include <QStringList>
#include <QVector>

struct BookInfo
{
    int id = 0;
    QString isbn;
    QString title;
    QString author;
    QString category;
    int totalCopies = 0;
    int availableCopies = 0;
};

struct AvailableCopyInfo
{
    int id = 0;
    QString code;
    QString title;
    QString author;
};

class BookService
{
public:
    bool addBook(const UserSession &session,
                 const QString &title,
                 const QString &author,
                 const QString &isbn,
                 const QString &category,
                 int copyCount,
                 QString *error);
    QVector<BookInfo> books(QString *error) const;
    QStringList categories(QString *error) const;
    QVector<AvailableCopyInfo> availableCopies(QString *error) const;
};
