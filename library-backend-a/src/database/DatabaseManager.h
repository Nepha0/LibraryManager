#ifndef LIB_DATABASE_MANAGER_H
#define LIB_DATABASE_MANAGER_H
/*
 * DatabaseManager —— 数据库连接管理与建表（后端A 提供，全组共用）
 *
 * V1.1 适配（统一 MySQL DDL）：
 *  - 双驱动：openSqlite() 用于单元测试与本地演示（:memory: 或文件）；
 *            openMySql() 用 QODBC + DSN 连接 B 建好的 MySQL 库。
 *  - MySQL 模式不执行内嵌建表——表结构由 docs/统一MySQL-DDL.md
 *    （空库）或 003_unified_schema_v1.sql（旧库）统一创建，避免两套 DDL 分裂。
 *  - SQLite 内嵌 schema 的物理表名/列名/枚举值已对齐 V1.1，保证测试与生产同构。
 *  - nowExpr(db)：SQL 片段级方言隔离（SQLite=datetime('now','localtime')，MySQL=NOW()）。
 */
#include <QSqlDatabase>
#include <QString>

namespace lib {

class DatabaseManager {
public:
    static DatabaseManager& instance();

    // 打开 SQLite 数据库（path 传 ":memory:" 可用于单元测试）
    bool open(const QString& sqlitePath);
    bool openSqlite(const QString& sqlitePath) { return open(sqlitePath); }

    // 打开 MySQL（QODBC + DSN）。传入完整 ODBC 连接串，例如：
    //   "DSN=LibraryMySQL;UID=root;PWD=xxx;DATABASE=library_manager;CHARSET=utf8mb4"
    // 连接串也可写成无 DSN 的 "DRIVER={MySQL ODBC 8.0 Unicode Driver};SERVER=...;..."。
    bool openMySql(const QString& odbcConnectionString);
    // 便捷重载：DSN + 库名 + 账号密码（自动带 utf8mb4）
    bool openMySqlByDsn(const QString& dsn, const QString& dbName,
                        const QString& user, const QString& password);

    void close();

    QSqlDatabase db() const { return m_db; }

    // 是否运行在 MySQL 模式
    bool isMySql() const { return m_mySql; }

    // 执行内嵌 SCHEMA_SQL（仅 SQLite；MySQL 模式直接返回 true，不碰表结构）。
    bool initSchema(QString* errorOut = nullptr);

    // 方言隔离：返回更新记录时间的 SQL 表达式（裸 SQL 片段，不含引号包裹）
    static QString nowExpr(const QSqlDatabase& db);
    QString nowExpr() const { return nowExpr(m_db); }

    QString lastError() const { return m_lastError; }

    // 9 张业务表 DDL（SQLite 版，结构与 V1.1 对齐；docs/统一MySQL-DDL.md 为此基线）
    static const char* schemaSql();

private:
    DatabaseManager() = default;
    QSqlDatabase m_db;
    QString m_lastError;
    bool m_mySql = false;
};

} // namespace lib

#endif // LIB_DATABASE_MANAGER_H
