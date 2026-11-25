#pragma once

#include "../auth/authservice.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

class CirculationService;

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
    // 统一契约入口。fields 支持 reader_no、name、phone。
    lib::Result addReader(const lib::OperatorContext &context,
                          const QVariantMap &fields);

    lib::Result updateReader(const lib::OperatorContext &context,
                             qint64 readerId,
                             const QVariantMap &fields);

    lib::Result setReaderStatus(const lib::OperatorContext &context,
                                qint64 readerId,
                                bool enabled);

    lib::Result checkReaderEligibility(
        const lib::OperatorContext &context,
        qint64 readerId) const;

    // C 的 EligibilityFn 适配入口：只返回读者资格，不暴露 B 的会话类型。
    lib::Result checkReaderEligibility(qint64 readerId) const;

    lib::Result getReader(const lib::OperatorContext &context,
                          qint64 readerId) const;

    lib::Result listReaders(const lib::OperatorContext &context,
                            const QVariantMap &filter,
                            int page = 1,
                            int pageSize = 20) const;

private:
    friend class CirculationService;

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
