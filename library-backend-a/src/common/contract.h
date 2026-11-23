#ifndef LIB_CONTRACT_H
#define LIB_CONTRACT_H
/*
 * contract.h —— 图书馆管理系统 后端A 统一数据契约
 * 板块二（图书与馆藏业务）：BookService / CategoryService / CopyService /
 *                          SearchService / AdvancedSearchService / ImportExportService
 *
 * 本文件为 A/B/C 三方与前端共同确认的契约基础（阶段0产出物）：
 *  - 统一返回结构 Result{code, message, data, list, total}
 *  - 统一错误码枚举（前端按 code 分支展示，message 仅作透出文案）
 *  - 图书状态 / 副本状态枚举（副本状态机的唯一合法转换源在 CopyService）
 */
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QDate>

namespace lib {

// ===================== 角色枚举（正式权限拦截在 B 板块，A 侧做防御性校验） =====================
enum class Role { Admin, Librarian, Reader };

inline QString roleName(Role r)
{
    switch (r) {
    case Role::Admin:     return QStringLiteral("管理员");
    case Role::Librarian: return QStringLiteral("图书管理员");
    case Role::Reader:    return QStringLiteral("读者");
    }
    return QStringLiteral("未知");
}

// 会话操作者上下文：由 B 板块 AuthService 生成，Service 层据此做防御性权限校验
struct OperatorContext {
    qint64  userId = 0;
    QString userName;
    Role    role = Role::Reader;
    bool    authenticated = false;

    bool isStaff() const { return authenticated && (role == Role::Admin || role == Role::Librarian); }
    bool isAdmin() const { return authenticated && role == Role::Admin; }
};

// ===================== 图书状态（A 板块牵头定义） =====================
enum class BookStatus { Active, ShelvedOff };

inline QString bookStatusToString(BookStatus s)
{
    return s == BookStatus::Active ? QStringLiteral("ACTIVE") : QStringLiteral("SHELVED_OFF");
}

inline BookStatus bookStatusFromString(const QString& s, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (s == QStringLiteral("ACTIVE"))      return BookStatus::Active;
    if (s == QStringLiteral("SHELVED_OFF")) return BookStatus::ShelvedOff;
    if (ok) *ok = false;
    return BookStatus::Active;
}

inline QString bookStatusDisplayName(const QString& s)
{
    return s == QStringLiteral("SHELVED_OFF") ? QStringLiteral("已下架") : QStringLiteral("正常");
}

// ===================== 副本状态（A 板块牵头定义，状态机见 CopyService） =====================
enum class CopyStatus { Available, Borrowed, Reserved, Maintenance, Lost };

inline QString copyStatusToString(CopyStatus s)
{
    switch (s) {
    case CopyStatus::Available:   return QStringLiteral("AVAILABLE");
    case CopyStatus::Borrowed:    return QStringLiteral("BORROWED");
    case CopyStatus::Reserved:    return QStringLiteral("RESERVED");
    case CopyStatus::Maintenance: return QStringLiteral("MAINTENANCE");
    case CopyStatus::Lost:        return QStringLiteral("LOST");
    }
    return QStringLiteral("AVAILABLE");
}

inline CopyStatus copyStatusFromString(const QString& s, bool* ok = nullptr)
{
    if (ok) *ok = true;
    if (s == QStringLiteral("AVAILABLE"))   return CopyStatus::Available;
    if (s == QStringLiteral("BORROWED"))    return CopyStatus::Borrowed;
    if (s == QStringLiteral("RESERVED"))    return CopyStatus::Reserved;
    if (s == QStringLiteral("MAINTENANCE")) return CopyStatus::Maintenance;
    if (s == QStringLiteral("LOST"))        return CopyStatus::Lost;
    if (ok) *ok = false;
    return CopyStatus::Available;
}

inline QString copyStatusDisplayName(const QString& s)
{
    if (s == QStringLiteral("AVAILABLE"))   return QStringLiteral("在馆可借");
    if (s == QStringLiteral("BORROWED"))    return QStringLiteral("借出");
    if (s == QStringLiteral("RESERVED"))    return QStringLiteral("预约保留待取");
    if (s == QStringLiteral("MAINTENANCE")) return QStringLiteral("维修中");
    if (s == QStringLiteral("LOST"))        return QStringLiteral("遗失报损");
    return s;
}

// ===================== 统一错误码（前端据 code 分支，禁止只返回"操作失败"） =====================
enum class ErrorCode {
    Success = 0,
    NotAuthenticated        = 1001, // 未登录 / 会话失效
    NoPermission            = 1002, // 无权限（如读者修改馆藏、非管理员操作维修/报损副本）
    MissingField            = 1003, // 必填字段缺失
    ForeignKeyNotFound      = 1004, // 外键不存在（如分类不存在）
    IsbnDuplicate           = 1005, // ISBN 重复
    BarcodeDuplicate        = 1006, // 副本条码重复
    IllegalStatusTransition = 1007, // 非法状态转换
    HasBorrowRecords        = 1008, // 存在借阅记录，拒绝物理删除（提示改用下架）
    CategoryInUse           = 1009, // 分类被图书引用，拒绝删除
    BookNotFound            = 1010, // 图书不存在
    CopyNotFound            = 1011, // 副本不存在
    CategoryNotFound        = 1012, // 分类不存在
    BookShelvedOff          = 1013, // 图书已下架（不可再借出）
    EmptyCondition          = 1014, // 空条件（返回空结果而非全量）
    InvalidCondition        = 1015, // 检索条件非法
    InvalidPage             = 1016, // 分页参数非法
    NameDuplicate           = 1017, // 名称重复（分类名等）
    FileError               = 1018, // 文件读写错误
    FormatError             = 1019, // 文件格式错误
    DbError                 = 1999  // 数据库错误
};

// ===================== 统一返回结构 =====================
struct Result {
    ErrorCode    code = ErrorCode::Success;
    QString      message;      // 具体原因（透出给前端的文案）
    QVariantMap  data;         // 单条记录 / 统计数据
    QVariantList list;         // 记录列表（每项为 QVariantMap）
    int          total = 0;    // 分页查询的总记录数

    bool ok() const { return code == ErrorCode::Success; }

    static Result success(const QString& msg = QString())
    {
        Result r;
        r.code = ErrorCode::Success;
        r.message = msg;
        return r;
    }

    static Result fail(ErrorCode c, const QString& msg)
    {
        Result r;
        r.code = c;
        r.message = msg;
        return r;
    }
};

// 分页参数合法性校验（page 从 1 开始）
inline bool pageParamsValid(int page, int pageSize)
{
    return page >= 1 && pageSize >= 1;
}

} // namespace lib

#endif // LIB_CONTRACT_H
