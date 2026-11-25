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
        QString error;
        const auto result = ReaderService().checkReaderEligibility(session, readerId, &error);
        if (!error.isEmpty()) {
            return lib::EligibilityResult{false, true, {error}};
        }
        return lib::EligibilityResult{result.eligible,
            !result.readerEnabled || !result.accountEnabled, result.reasons};
    };
}

inline lib::LoanAuditFn loanAudit(const UserSession &session, const QString &action,
                                 const QString &detail)
{
    return [session, action, detail](qint64 loanId, QString *error) {
        return OperationLogService().record(session, action, QStringLiteral("loan"),
                                            QString::number(loanId), detail, error);
    };
}

} // namespace BackendIntegration
