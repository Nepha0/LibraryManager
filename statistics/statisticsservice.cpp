#include "statisticsservice.h"

#include "../common/database.h"

#include <QDateTime>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QVariant>

namespace {
QString csvCell(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}
}

DashboardStats StatisticsService::dashboard(QString *error) const
{
    DashboardStats stats;
    QSqlQuery query(Database::connection());
    if (!query.exec(QStringLiteral(
            "SELECT "
            "(SELECT COUNT(*) FROM books), "
            "(SELECT COUNT(*) FROM book_copies), "
            "(SELECT COUNT(*) FROM book_copies WHERE status = 'available'), "
            "(SELECT COUNT(*) FROM loans WHERE returned_at IS NULL), "
            "(SELECT COUNT(*) FROM loans "
            " WHERE returned_at IS NULL "
            " AND julianday(due_at) < julianday('now', 'localtime')), "
            "(SELECT COUNT(*) FROM readers WHERE active = 1)"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return stats;
    }
    if (query.next()) {
        stats.titles = query.value(0).toInt();
        stats.copies = query.value(1).toInt();
        stats.availableCopies = query.value(2).toInt();
        stats.activeLoans = query.value(3).toInt();
        stats.overdueLoans = query.value(4).toInt();
        stats.readers = query.value(5).toInt();
    }
    return stats;
}

QVector<LoanHistoryInfo>
StatisticsService::loanHistory(int limit, QString *error) const
{
    QVector<LoanHistoryInfo> result;
    QSqlQuery query(Database::connection());
    query.prepare(QStringLiteral(
        "SELECT r.reader_no, r.name, bc.copy_code, b.title, "
        "l.borrowed_at, l.due_at, l.returned_at "
        "FROM loans l "
        "JOIN readers r ON r.id = l.reader_id "
        "JOIN book_copies bc ON bc.id = l.copy_id "
        "JOIN books b ON b.id = bc.book_id "
        "ORDER BY l.id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return result;
    }
    while (query.next()) {
        LoanHistoryInfo loan;
        loan.readerNo = query.value(0).toString();
        loan.readerName = query.value(1).toString();
        loan.copyCode = query.value(2).toString();
        loan.title = query.value(3).toString();
        loan.borrowedAt =
            QDateTime::fromString(query.value(4).toString(), Qt::ISODate);
        loan.dueAt =
            QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
        loan.returnedAt =
            QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        result.append(loan);
    }
    return result;
}

bool StatisticsService::exportCsv(const QString &fileName, QString *error) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法写入文件：%1").arg(file.errorString());
        }
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QChar(0xFEFF);
    stream << QStringLiteral("读者编号,读者姓名,册编号,书名,借出时间,应还时间,归还时间\n");

    QSqlQuery query(Database::connection());
    if (!query.exec(QStringLiteral(
            "SELECT r.reader_no, r.name, bc.copy_code, b.title, "
            "l.borrowed_at, l.due_at, l.returned_at "
            "FROM loans l "
            "JOIN readers r ON r.id = l.reader_id "
            "JOIN book_copies bc ON bc.id = l.copy_id "
            "JOIN books b ON b.id = bc.book_id "
            "ORDER BY l.id"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    while (query.next()) {
        stream << csvCell(query.value(0).toString()) << ','
               << csvCell(query.value(1).toString()) << ','
               << csvCell(query.value(2).toString()) << ','
               << csvCell(query.value(3).toString()) << ','
               << csvCell(query.value(4).toString()) << ','
               << csvCell(query.value(5).toString()) << ','
               << csvCell(query.value(6).toString()) << '\n';
    }
    file.close();
    return true;
}
