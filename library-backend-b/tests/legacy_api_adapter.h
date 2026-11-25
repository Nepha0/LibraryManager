#pragma once

// Test-only adapters.  The smoke test historically used the pre-contract
// signatures; these adapters keep that test readable while exercising only
// the public Result + OperatorContext entry points.

#include "library-backend-b/src/auth/authservice.h"
#include "library-backend-b/src/auth/permissionservice.h"
#include "library-backend-b/src/auth/userservice.h"
#include "library-backend-b/src/readers/readerservice.h"
#include "library-backend-b/src/system/operationlogservice.h"
#include "library-backend-b/src/system/paramservice.h"

namespace test_legacy {

inline lib::Role roleFromString(const QString &role)
{
    if (role == QStringLiteral("admin")) {
        return lib::Role::Admin;
    }
    if (role == QStringLiteral("librarian")) {
        return lib::Role::Librarian;
    }
    return lib::Role::Reader;
}

inline lib::OperatorContext contextFromResult(const lib::Result &result)
{
    const QVariantMap &data = result.data;
    return {
        data.value(QStringLiteral("user_id")).toLongLong(),
        data.value(QStringLiteral("user_name")).toString(),
        roleFromString(data.value(QStringLiteral("role")).toString()),
        data.value(QStringLiteral("authenticated")).toBool(),
        data.value(QStringLiteral("session_generation")).toLongLong()};
}

inline lib::OperatorContext contextFromSession(const UserSession &session)
{
    return session.operatorContext();
}

inline lib::OperatorContext &lastContext()
{
    static lib::OperatorContext context;
    return context;
}

class AuthService
{
public:
    static lib::Result login(const QString &username,
                             const QString &password)
    {
        const lib::Result result = ::AuthService::login(username, password);
        // The historical no-context parameter helpers need one stable
        // operator. Keep the first successful (admin) context instead of
        // replacing it with temporary test accounts whose sessions are later
        // deliberately invalidated.
        if (result.ok() && !lastContext().authenticated) {
            lastContext() = contextFromResult(result);
        }
        return result;
    }

    static std::optional<UserSession> login(const QString &username,
                                            const QString &password,
                                            QString *error)
    {
        const lib::Result result = login(username, password);
        if (!result.ok()) {
            if (error) {
                *error = result.message;
            }
            return std::nullopt;
        }

        UserSession session =
            UserSession::fromOperatorContext(contextFromResult(result));
        session.displayName = result.data.value(
            QStringLiteral("display_name")).toString();
        if (error) {
            error->clear();
        }
        return session;
    }

    static void logout(UserSession &session)
    {
        session.clear();
        lastContext() = {};
    }
};

class PermissionService
{
public:
    static bool hasPermission(const UserSession &session,
                              ::Permission permission)
    {
        return ::PermissionService::hasPermission(
            contextFromSession(session), permission);
    }

    static bool hasPermission(const lib::OperatorContext &context,
                              ::Permission permission)
    {
        return ::PermissionService::hasPermission(context, permission);
    }

    static bool requirePermission(const UserSession &session,
                                  ::Permission permission,
                                  QString *error)
    {
        const lib::Result result = ::PermissionService::requirePermission(
            contextFromSession(session), permission);
        if (error) {
            *error = result.ok() ? QString() : result.message;
        }
        return result.ok();
    }

    static lib::Result requirePermission(
        const lib::OperatorContext &context,
        ::Permission permission)
    {
        return ::PermissionService::requirePermission(context, permission);
    }
};

inline UserInfo userFromResult(const lib::Result &result)
{
    const QVariantMap &data = result.data;
    UserInfo user;
    user.id = data.value(QStringLiteral("user_id")).toLongLong();
    user.username = data.value(QStringLiteral("username"),
                               data.value(QStringLiteral("user_name"))).toString();
    user.displayName = data.value(QStringLiteral("display_name")).toString();
    user.role = data.value(QStringLiteral("role")).toString();
    user.enabled = data.value(QStringLiteral("enabled")).toBool();
    user.createdAt = data.value(QStringLiteral("created_at")).toDateTime();
    return user;
}

class UserService
{
public:
    bool addUser(const ::UserSession &session,
                 const QString &username,
                 const QString &displayName,
                 const QString &password,
                 const QString &role,
                 QString *error)
    {
        const lib::Result result = service.addUser(
            contextFromSession(session),
            {{QStringLiteral("username"), username},
             {QStringLiteral("display_name"), displayName},
             {QStringLiteral("password"), password},
             {QStringLiteral("role"), role}});
        return finish(result, error);
    }

    bool updateUser(const ::UserSession &session,
                    qlonglong userId,
                    const QString &username,
                    const QString &displayName,
                    QString *error)
    {
        const lib::Result result = service.updateUser(
            contextFromSession(session), userId,
            {{QStringLiteral("username"), username},
             {QStringLiteral("display_name"), displayName}});
        return finish(result, error);
    }

    bool resetPassword(const ::UserSession &session,
                       qlonglong userId,
                       const QString &password,
                       QString *error)
    {
        return finish(service.resetPassword(
                           contextFromSession(session), userId, password),
                       error);
    }

    bool assignRole(const ::UserSession &session,
                    qlonglong userId,
                    const QString &role,
                    QString *error)
    {
        return finish(service.assignRole(
                           contextFromSession(session), userId, role),
                       error);
    }

    bool setUserStatus(const ::UserSession &session,
                       qlonglong userId,
                       bool enabled,
                       QString *error)
    {
        return finish(service.setUserStatus(
                           contextFromSession(session), userId, enabled),
                       error);
    }

    std::optional<UserInfo> getUser(const ::UserSession &session,
                                    qlonglong userId,
                                    QString *error) const
    {
        const lib::Result result = service.getUser(
            contextFromSession(session), userId);
        if (!finish(result, error)) {
            return std::nullopt;
        }
        return userFromResult(result);
    }

    UserPage listUsers(const ::UserSession &session,
                       const UserQuery &query,
                       QString *error) const
    {
        QVariantMap filter;
        filter.insert(QStringLiteral("keyword"), query.keyword);
        filter.insert(QStringLiteral("role"), query.role);
        if (query.enabled.has_value()) {
            filter.insert(QStringLiteral("enabled"), *query.enabled);
        }
        const lib::Result result = service.listUsers(
            contextFromSession(session), filter, query.page, query.pageSize);
        UserPage page;
        page.page = query.page;
        page.pageSize = query.pageSize;
        page.total = result.total;
        if (!finish(result, error)) {
            return page;
        }
        for (const QVariant &value : result.list) {
            const QVariantMap data = value.toMap();
            lib::Result item = lib::Result::success();
            item.data = data;
            page.items.append(userFromResult(item));
        }
        return page;
    }

private:
    static bool finish(const lib::Result &result, QString *error)
    {
        if (error) {
            *error = result.ok() ? QString() : result.message;
        }
        return result.ok();
    }

    ::UserService service;
};

inline ReaderInfo readerFromMap(const QVariantMap &data)
{
    ReaderInfo reader;
    reader.id = data.value(QStringLiteral("reader_id")).toInt();
    reader.readerNo = data.value(QStringLiteral("reader_no")).toString();
    reader.name = data.value(QStringLiteral("name")).toString();
    reader.phone = data.value(QStringLiteral("phone")).toString();
    reader.activeLoans = data.value(QStringLiteral("active_loans")).toInt();
    reader.enabled = data.value(QStringLiteral("enabled")).toBool();
    return reader;
}

class ReaderService
{
public:
    bool addReader(const ::UserSession &session,
                   const QString &readerNo,
                   const QString &name,
                   const QString &phone,
                   QString *error)
    {
        return finish(service.addReader(
                           contextFromSession(session),
                           {{QStringLiteral("reader_no"), readerNo},
                            {QStringLiteral("name"), name},
                            {QStringLiteral("phone"), phone}}),
                       error);
    }

    bool updateReader(const ::UserSession &session,
                      int readerId,
                      const QString &readerNo,
                      const QString &name,
                      const QString &phone,
                      QString *error)
    {
        return finish(service.updateReader(
                           contextFromSession(session), readerId,
                           {{QStringLiteral("reader_no"), readerNo},
                            {QStringLiteral("name"), name},
                            {QStringLiteral("phone"), phone}}),
                       error);
    }

    bool setReaderStatus(const ::UserSession &session,
                         int readerId,
                         bool enabled,
                         QString *error)
    {
        return finish(service.setReaderStatus(
                           contextFromSession(session), readerId, enabled),
                       error);
    }

    ReaderEligibilityResult checkReaderEligibility(
        const ::UserSession &session,
        int readerId,
        QString *error) const
    {
        const lib::Result result = service.checkReaderEligibility(
            contextFromSession(session), readerId);
        ReaderEligibilityResult eligibility;
        if (!finish(result, error)) {
            return eligibility;
        }
        const QVariantMap &data = result.data;
        eligibility.eligible = data.value(QStringLiteral("eligible")).toBool();
        eligibility.readerEnabled = data.value(QStringLiteral("reader_enabled")).toBool();
        eligibility.accountEnabled = data.value(QStringLiteral("account_enabled")).toBool();
        eligibility.activeLoans = data.value(QStringLiteral("active_loans")).toInt();
        eligibility.borrowLimit = data.value(QStringLiteral("borrow_limit")).toInt();
        for (const QVariant &value : data.value(QStringLiteral("reasons")).toList()) {
            eligibility.reasons.append(value.toString());
        }
        eligibility.reason = eligibility.reasons.join(QStringLiteral("；"));
        return eligibility;
    }

    std::optional<ReaderDetail> getReader(const ::UserSession &session,
                                           int readerId,
                                           QString *error) const
    {
        const lib::Result result = service.getReader(
            contextFromSession(session), readerId);
        if (!finish(result, error)) {
            return std::nullopt;
        }
        ReaderDetail detail;
        detail.info = readerFromMap(result.data);
        detail.totalLoans = result.data.value(QStringLiteral("total_loans")).toInt();
        detail.returnedLoans = result.data.value(QStringLiteral("returned_loans")).toInt();
        detail.overdueLoans = result.data.value(QStringLiteral("overdue_loans")).toInt();
        return detail;
    }

    QVector<ReaderInfo> readers(const ::UserSession &session,
                                const ReaderQuery &query,
                                QString *error) const
    {
        QVariantMap filter;
        filter.insert(QStringLiteral("reader_no"), query.readerNo);
        filter.insert(QStringLiteral("name"), query.name);
        if (query.enabled.has_value()) {
            filter.insert(QStringLiteral("enabled"), *query.enabled);
        }
        const lib::Result result = service.listReaders(
            contextFromSession(session), filter,
            1, 200);
        QVector<ReaderInfo> items;
        if (!finish(result, error)) {
            return items;
        }
        for (const QVariant &value : result.list) {
            items.append(readerFromMap(value.toMap()));
        }
        return items;
    }

    QVector<ReaderInfo> readers(const ::UserSession &session,
                                QString *error) const
    {
        return readers(session, ReaderQuery{}, error);
    }

private:
    static bool finish(const lib::Result &result, QString *error)
    {
        if (error) {
            *error = result.ok() ? QString() : result.message;
        }
        return result.ok();
    }

    ::ReaderService service;
};

class ParamService
{
public:
    std::optional<QString> getParam(const QString &key,
                                    QString *error) const
    {
        const lib::Result result = service.getParam(lastContext(), key);
        if (!finish(result, error)) {
            return std::nullopt;
        }
        return result.data.value(QStringLiteral("value")).toString();
    }

    std::optional<int> getIntParam(const QString &key,
                                   QString *error) const
    {
        const lib::Result result = service.getIntParam(lastContext(), key);
        if (!finish(result, error)) {
            return std::nullopt;
        }
        return result.data.value(QStringLiteral("value")).toInt();
    }

    bool setParam(const ::UserSession &session,
                  const QString &key,
                  const QString &value,
                  QString *error)
    {
        return finish(service.setParam(
                           contextFromSession(session), key, value), error);
    }

private:
    static bool finish(const lib::Result &result, QString *error)
    {
        if (error) {
            *error = result.ok() ? QString() : result.message;
        }
        return result.ok();
    }

    ::ParamService service;
};

class OperationLogService
{
public:
    bool record(const ::UserSession &session,
                const QString &actionType,
                const QString &targetType,
                const QString &targetId,
                const QString &detail,
                QString *error)
    {
        return finish(service.record(contextFromSession(session), actionType,
                                     targetType, targetId, detail), error);
    }

    OperationLogPage listLogs(const ::UserSession &session,
                              const OperationLogQuery &query,
                              QString *error) const
    {
        QVariantMap filter;
        filter.insert(QStringLiteral("action_type"), query.actionType);
        if (query.from.isValid()) {
            filter.insert(QStringLiteral("from"), query.from);
        }
        if (query.to.isValid()) {
            filter.insert(QStringLiteral("to"), query.to);
        }
        const lib::Result result = service.listLogs(
            contextFromSession(session), filter, query.page, query.pageSize);
        OperationLogPage page;
        page.page = query.page;
        page.pageSize = query.pageSize;
        page.total = result.total;
        if (!finish(result, error)) {
            return page;
        }
        for (const QVariant &value : result.list) {
            const QVariantMap data = value.toMap();
            OperationLogInfo item;
            item.id = data.value(QStringLiteral("log_id")).toLongLong();
            item.operatorUserId = data.value(QStringLiteral("operator_user_id")).toLongLong();
            item.operatorName = data.value(QStringLiteral("operator_name")).toString();
            item.actionType = data.value(QStringLiteral("action_type")).toString();
            item.targetType = data.value(QStringLiteral("target_type")).toString();
            item.targetId = data.value(QStringLiteral("target_id")).toString();
            item.detail = data.value(QStringLiteral("detail")).toString();
            item.createdAt = data.value(QStringLiteral("created_at")).toDateTime();
            page.items.append(item);
        }
        return page;
    }

private:
    static bool finish(const lib::Result &result, QString *error)
    {
        if (error) {
            *error = result.ok() ? QString() : result.message;
        }
        return result.ok();
    }

    ::OperationLogService service;
};

} // namespace test_legacy
