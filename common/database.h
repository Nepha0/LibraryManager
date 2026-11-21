#pragma once

#include <QSqlDatabase>
#include <QString>

class Database
{
public:
    static bool initialize(QString *error = nullptr);
    static void close();
    static QSqlDatabase connection();
};