#pragma once

#include "../library-backend-b/src/readers/readerservice.h"
#include "../library-backend-b/src/system/operationlogservice.h"
#include "../library-backend-c/src/contract_c.h"

namespace BackendIntegration {

inline lib::OperatorContext operatorContext(const UserSession &session)
{
    auto role = lib::Role::Reader;
    if (session.isAdmin()) {
        role = lib::Role::Admin;
    } else if (session.isLibrarian()) {
        role = lib::Role::Librarian;
    }
    return {session.userId, session.username, role, session.isAuthenticated(),
            static_cast<qint64>(session.generation)};
}

inline lib::EligibilityFn readerEligibility(const UserSession &session)
{
    return [session](qint64 readerId) {
        const lib::Result result = ReaderService().checkReaderEligibility(
            session.operatorContext(), readerId);
        if (!result.ok()) {
            return lib::EligibilityResult{false, true, {result.message}};
        }

        const QVariantMap data = result.data;
        QStringList reasons;
        for (const QVariant &value : data.value(QStringLiteral("reasons")).toList()) {
            reasons.append(value.toString());
        }
        const bool accountProblem =
            !data.value(QStringLiteral("reader_enabled")).toBool()
            || !data.value(QStringLiteral("account_enabled")).toBool();
        return lib::EligibilityResult{
            data.value(QStringLiteral("eligible")).toBool(),
            accountProblem,
            reasons};
    };
}

inline lib::LoanAuditFn loanAudit(const UserSession &session, const QString &action,
                                 const QString &detail)
{
    return [session, action, detail](qint64 loanId, QString *error) {
        const lib::Result result = OperationLogService().record(
            session.operatorContext(), action, QStringLiteral("loan"),
            QString::number(loanId), detail);
        if (error) {
            *error = result.ok() ? QString() : result.message;
        }
        return result.ok();
    };
}

} // namespace BackendIntegration
