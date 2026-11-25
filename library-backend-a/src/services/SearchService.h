#ifndef LIB_SEARCH_SERVICE_H
#define LIB_SEARCH_SERVICE_H
/*
 * SearchService —— 关键词检索（P0）
 * 完成标准对应 2.2「关键词检索」：
 *  - search(关键词, 分页)：书名 / 作者 / ISBN 模糊匹配
 *  - 结果附带各图书可借数量与馆藏位置
 *  - 空关键词返回空结果而非全量
 */
#include "common/contract.h"
#include <QSqlDatabase>

namespace lib {

class SearchService {
public:
    explicit SearchService(QSqlDatabase db);

    Result search(const OperatorContext& op, const QString& keyword,
                  int page = 1, int pageSize = 10);

private:
    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_SEARCH_SERVICE_H
