#ifndef LIB_RECOMMEND_SERVICE_H
#define LIB_RECOMMEND_SERVICE_H
/*
 * RecommendService —— 规则化推荐（P2）
 * 契约见 library-backend-c/docs/接口契约-C.md §8。
 *
 * 规则：优先"读者历史借阅中出现最多的 2~3 个分类下的在馆可借图书"；
 * 不足回退"全馆近期借阅热度 Top N"（冷启动）；排除已借过、已下架图书；
 * 结果全部来自真实馆藏，不做模型训练；接口失败由前端回退检索入口。
 * list 行复用 A §10 检索结果行结构。
 * 权限同 PersonalService（只能给自己推荐）。
 */
#include "../contract_c.h"

namespace lib {

class RecommendService {
public:
    explicit RecommendService(QSqlDatabase db);

    Result recommend(const OperatorContext& op, qint64 readerId);
    // → list 行: { book_id, isbn, title, author, publisher, publish_date, category_name,
    //              copy_total, available_count, borrowable, status_text, locations_text }

private:
    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_RECOMMEND_SERVICE_H
