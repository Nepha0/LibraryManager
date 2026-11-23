#ifndef LIB_DATABASE_MANAGER_H
#define LIB_DATABASE_MANAGER_H
/*
 * DatabaseManager —— SQLite 连接管理与建表（后端A 提供，全组共用）
 *
 * 说明：
 *  - 阶段0 契约：9 张核心表 DDL 全部在此（A 牵头图书/分类/副本 3 张，
 *    其余 6 张按契约建结构，供 B/C 与 A 的删除校验使用）。
 *  - 表结构若经全组评审变更，需同步修改本文件内嵌 SCHEMA_SQL 与 docs/schema.sql。
 */
#include <QSqlDatabase>
#include <QString>

namespace lib {

class DatabaseManager {
public:
    static DatabaseManager& instance();

    // 打开 SQLite 数据库（path 传 ":memory:" 可用于单元测试）
    bool open(const QString& sqlitePath);
    void close();

    QSqlDatabase db() const { return m_db; }

    // 执行内嵌 SCHEMA_SQL（幂等：CREATE TABLE IF NOT EXISTS）
    bool initSchema(QString* errorOut = nullptr);

    QString lastError() const { return m_lastError; }

    // 9 张核心表 DDL（单一事实来源，docs/schema.sql 与此同步）
    static const char* schemaSql();

private:
    DatabaseManager() = default;
    QSqlDatabase m_db;
    QString m_lastError;
};

} // namespace lib

#endif // LIB_DATABASE_MANAGER_H
