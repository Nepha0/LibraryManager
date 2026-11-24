#include "RecommendService.h"

namespace lib {

RecommendService::RecommendService(QSqlDatabase db)
    : m_db(db) {}

// TODO(P2)：按契约 §8 规则化推荐实现（分类偏好 + 热度回退）
Result RecommendService::recommend(const OperatorContext& op, qint64 readerId)
{
    Q_UNUSED(op); Q_UNUSED(readerId);
    return failC(ErrorCodeC::NotImplemented, QStringLiteral("RecommendService::recommend 尚未实现（P2 交付）"));
}

} // namespace lib
