#ifndef LIB_CONTRACT_C_H
#define LIB_CONTRACT_C_H
/*
 * contract_c.h —— 后端C 统一数据契约扩展（借阅流通/预约/逾期/推荐）
 *
 * 依赖并复用 A 端 contract.h（Result / OperatorContext / 错误码 1000~1999 / 状态枚举）。
 * 本文件只补充 C 自有内容：
 *  - 错误码 3000~3999 段（见 library-backend-c/docs/接口契约-C.md §1，新增须登记）
 *  - 预约状态枚举辅助（与统一 MySQL DDL 的 ENUM 字面量一致，小写）
 *  - 读者借阅资格端口（EligibilityFn）：B 板块 checkReaderEligibility 的适配点，
 *    C 的 Service 不直接依赖 B 的 UserSession 风格；测试可注入 mock
 *  - 系统参数读取辅助（system_params 表只读，键名以 B 端为准）
 *
 * 日期口径：借出/应还/归还均为 DATE（天粒度，逾期天数=日期差）；
 * C 侧统一在 C++ 里用 QDate 计算后绑定，避免 MySQL/SQLite 方言差异。
 */
#include "common/contract.h"

#include <QDate>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <functional>
#include <optional>

namespace lib {

// ===================== C 段错误码（3000~3999） =====================
enum class ErrorCodeC {
    ReaderNotEligible            = 3001, // 读者资格不通过（message 逐条列出 B 返回原因）
    CopyNotAvailable             = 3002, // 该副本当前不可借（message 含当前状态中文名）
    LoanNotFound                 = 3003, // 借阅记录不存在或已归还
    RenewRejected                = 3004, // 续借不通过（message 逐条列出全部原因）
    DuplicateReservation         = 3005, // 同一读者同书已有排队中/待取预约
    ReservationNotAllowed        = 3006, // 该书当前有可借副本，不可提交预约
    ReservationNotFound          = 3007, // 预约记录不存在
    IllegalReservationTransition = 3008, // 预约状态流转非法（message 含当前状态）
    NoQueueToPromote             = 3009, // 无可推进的排队预约
    NotImplemented               = 3999  // 功能尚未实现（骨架期占位，P1/P2 交付后移除）
};

// C 段错误码 → 统一 Result（F 端按数字 code 分支，无需感知枚举类型）
inline Result failC(ErrorCodeC c, const QString& msg)
{
    return Result::fail(static_cast<ErrorCode>(static_cast<int>(c)), msg);
}

// 统一错误码转换：事务回滚等泛型路径中 ErrorCode/ErrorCodeC 均可传入
inline ErrorCode toErrorCode(ErrorCode c) { return c; }
inline ErrorCode toErrorCode(ErrorCodeC c) { return static_cast<ErrorCode>(static_cast<int>(c)); }

// ===================== 预约状态（C 板块牵头定义，状态机见 ReservationService） =====================
enum class ResvStatus { Waiting, ReadyToPick, Finished, Cancelled, Expired };

inline QString resvStatusToString(ResvStatus s)
{
    switch (s) {
    case ResvStatus::Waiting:     return QStringLiteral("waiting");
    case ResvStatus::ReadyToPick: return QStringLiteral("ready_to_pick");
    case ResvStatus::Finished:    return QStringLiteral("finished");
    case ResvStatus::Cancelled:   return QStringLiteral("cancelled");
    case ResvStatus::Expired:     return QStringLiteral("expired");
    }
    return QStringLiteral("waiting");
}

inline ResvStatus resvStatusFromString(const QString& s, bool* ok = nullptr)
{
    if (ok) *ok = true;
    const QString v = s.trimmed().toLower();
    if (v == QStringLiteral("waiting"))       return ResvStatus::Waiting;
    if (v == QStringLiteral("ready_to_pick")) return ResvStatus::ReadyToPick;
    if (v == QStringLiteral("finished"))      return ResvStatus::Finished;
    if (v == QStringLiteral("cancelled"))     return ResvStatus::Cancelled;
    if (v == QStringLiteral("expired"))       return ResvStatus::Expired;
    if (ok) *ok = false;
    return ResvStatus::Waiting;
}

inline QString resvStatusDisplayName(const QString& s)
{
    const QString v = s.trimmed().toLower();
    if (v == QStringLiteral("waiting"))       return QStringLiteral("排队中");
    if (v == QStringLiteral("ready_to_pick")) return QStringLiteral("待取");
    if (v == QStringLiteral("finished"))      return QStringLiteral("已完成");
    if (v == QStringLiteral("cancelled"))     return QStringLiteral("已取消");
    if (v == QStringLiteral("expired"))       return QStringLiteral("已失效");
    return s;
}

// ===================== 读者借阅资格端口（B 板块统一出口的适配点） =====================
/*
 * B 板块 checkReaderEligibility 的返回裁剪视图：
 *  - eligible      ：是否通过全部资格校验
 *  - accountProblem：是否存在"读者状态/账号状态"类原因（续借场景只消费此类，借阅上限不参与续借判断）
 *  - reasons       ：全部不通过原因（中文，可直接透出）
 * 接线（app 集成层）由 B 板块提供：把 UserSession 风格的 checkReaderEligibility 适配成本函数；
 * 单元测试注入 mock。
 */
struct EligibilityResult {
    bool        eligible = false;
    bool        accountProblem = false;
    QStringList reasons;
};

using EligibilityFn = std::function<EligibilityResult(qint64 readerId)>;
// 可选的操作日志写入，同业务事务使用同一数据库连接。
using LoanAuditFn = std::function<bool(qint64 loanId, QString *error)>;

// ===================== 系统参数只读辅助（键名以 B 端 system_params 为准） =====================
// 业务层（借阅/续借/预约）统一从参数表读取，禁止硬编码。
inline std::optional<int> readIntParam(const QSqlDatabase& db, const QString& key, QString* errOut = nullptr)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT param_value FROM system_params WHERE param_key = ?"));
    q.addBindValue(key);
    if (!q.exec() || !q.next()) {
        if (errOut) {
            *errOut = q.lastError().isValid()
                          ? QStringLiteral("读取系统参数 %1 失败: %2").arg(key, q.lastError().text())
                          : QStringLiteral("系统参数 %1 不存在").arg(key);
        }
        return std::nullopt;
    }
    bool ok = false;
    const int v = q.value(0).toString().trimmed().toInt(&ok);
    if (!ok) {
        if (errOut) *errOut = QStringLiteral("系统参数 %1 不是合法整数").arg(key);
        return std::nullopt;
    }
    return v;
}

// ODBC/MySQL 读回 DATE 列的统一取值（可能回 QDate 也可能回字符串）
inline QDate readDateValue(const QVariant& v)
{
    if (v.canConvert<QDate>()) return v.toDate();
    return QDate::fromString(v.toString(), QStringLiteral("yyyy-MM-dd"));
}

// 读回 DATETIME 列的统一取值（SQLite 存 TEXT，ODBC 回 QDateTime）
inline QDateTime readDateTimeValue(const QVariant& v)
{
    if (v.canConvert<QDateTime>()) {
        const QDateTime dt = v.toDateTime();
        if (dt.isValid()) return dt;
    }
    return QDateTime::fromString(v.toString(), Qt::ISODate);
}

} // namespace lib

#endif // LIB_CONTRACT_C_H
