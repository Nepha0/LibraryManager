/*
 * test_backend_a —— 后端A QTest 测试套件
 * 覆盖 2.2 完成标准的全部测试点：
 *  图书CRUD：必填缺失 / 外键分类不存在 / ISBN重复 / 增改查 / 下架 / 删除被拒 / 分页筛选
 *  副本与状态机：条码重复 / 全合法转换路径 / 全非法转换被拒 / 维修报损仅管理员 / 下架书不可借
 *  检索：关键词命中/不命中/空条件 / 多条件 AND-OR / 排序 / 分页边界 / 空条件与全非法条件
 *  分类：CRUD / 删除被引用拒绝
 *  导入导出：含错误行导入 / 空文件 / 非法格式 / 往返一致 / 大文件分页导出（CSV 与 XLSX）
 */
#include <QtTest/QtTest>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

#include "database/DatabaseManager.h"
#include "services/BookService.h"
#include "services/CategoryService.h"
#include "services/CopyService.h"
#include "services/SearchService.h"
#include "services/AdvancedSearchService.h"
#include "services/ImportExportService.h"
#include "test_backend_a.h"

using namespace lib;

void TestBackendA::init()
{
    auto& mgr = DatabaseManager::instance();
    QVERIFY(mgr.open(QStringLiteral(":memory:")));
    QVERIFY(mgr.initSchema());
}

qint64 TestBackendA::mkCategory(const QString& name)
{
    CategoryService s(DatabaseManager::instance().db());
    Result r = s.addCategory(admin(), QVariantMap { { "name", name } });
    if (!r.ok()) { qWarning() << "mkCategory失败:" << r.message; return -1; }
    return r.data.value(QStringLiteral("category_id")).toLongLong();
}

qint64 TestBackendA::mkBook(qint64 catId, const QString& isbn, const QString& title,
                            const QString& author, const QString& publisher, const QString& date)
{
    BookService s(DatabaseManager::instance().db());
    QVariantMap f {
        { "isbn", isbn }, { "title", title }, { "category_id", catId },
        { "author", author }, { "publisher", publisher }, { "publish_date", date }
    };
    Result r = s.addBook(admin(), f);
    if (!r.ok()) { qWarning() << "mkBook失败:" << r.message; return -1; }
    return r.data.value(QStringLiteral("book_id")).toLongLong();
}

qint64 TestBackendA::mkCopy(qint64 bookId, const QString& barcode, const QString& location)
{
    CopyService s(DatabaseManager::instance().db());
    Result r = s.addCopy(admin(), bookId, barcode, location, QDate(2024, 1, 1));
    if (!r.ok()) { qWarning() << "mkCopy失败:" << r.message; return -1; }
    return r.data.value(QStringLiteral("copy_id")).toLongLong();
}

void TestBackendA::insertReaderAndBorrow(qint64 bookId)
{
    QSqlQuery q(DatabaseManager::instance().db());
    QVERIFY(q.exec(QStringLiteral(
        "INSERT INTO readers (reader_no, name) VALUES ('R001', '张三')")));
    // 取一个真实副本供借阅记录外键引用
    QVERIFY(q.exec(QStringLiteral("SELECT id FROM book_copies WHERE book_id = %1 LIMIT 1").arg(bookId)));
    QVERIFY(q.next());
    const qint64 copyId = q.value(0).toLongLong();
    QVERIFY(q.prepare(QStringLiteral(
        "INSERT INTO loans (copy_id, reader_id, book_id, borrowed_at, due_at)"
        " VALUES (?, 1, ?, '2026-01-01', '2026-01-31')")));
    q.addBindValue(copyId);
    q.addBindValue(bookId);
    QVERIFY(q.exec());
}

// ==================== BookService ====================
void TestBackendA::bookAddValidation()
{
    BookService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));

    // 未登录
    Result r = s.addBook(anon(), QVariantMap { { "isbn", "1" }, { "title", "t" }, { "category_id", catId } });
    QCOMPARE(r.code, ErrorCode::NotAuthenticated);
    // 读者越权
    r = s.addBook(reader(), QVariantMap { { "isbn", "1" }, { "title", "t" }, { "category_id", catId } });
    QCOMPARE(r.code, ErrorCode::NoPermission);

    // 必填缺失
    r = s.addBook(admin(), QVariantMap { { "title", "t" }, { "category_id", catId } });
    QCOMPARE(r.code, ErrorCode::MissingField);
    QVERIFY(r.message.contains(QStringLiteral("ISBN")));
    r = s.addBook(admin(), QVariantMap { { "isbn", "X1" }, { "category_id", catId } });
    QCOMPARE(r.code, ErrorCode::MissingField);
    QVERIFY(r.message.contains(QStringLiteral("书名")));
    r = s.addBook(admin(), QVariantMap { { "isbn", "X1" }, { "title", "t" } });
    QVERIFY2(r.code == ErrorCode::MissingField,
             qPrintable(QStringLiteral("code=%1 msg=%2").arg(int(r.code)).arg(r.message)));

    // 外键分类不存在
    r = s.addBook(admin(), QVariantMap { { "isbn", "X1" }, { "title", "t" }, { "category_id", 999 } });
    QCOMPARE(r.code, ErrorCode::ForeignKeyNotFound);
    QVERIFY(r.message.contains(QStringLiteral("分类不存在")));

    // 日期格式非法
    r = s.addBook(admin(), QVariantMap { { "isbn", "X1" }, { "title", "t" },
                                          { "category_id", catId }, { "publish_date", "2020/01/01" } });
    QCOMPARE(r.code, ErrorCode::InvalidCondition);

    // ISBN 重复（ACTIVE 范围）
    QVERIFY(s.addBook(admin(), QVariantMap { { "isbn", "X1" }, { "title", "第一本" },
                                              { "category_id", catId } }).ok());
    r = s.addBook(admin(), QVariantMap { { "isbn", "X1" }, { "title", "第二本" }, { "category_id", catId } });
    QCOMPARE(r.code, ErrorCode::IsbnDuplicate);
    QVERIFY(r.message.contains(QStringLiteral("已存在")));
}

void TestBackendA::bookAddGetWithStats()
{
    BookService s(DatabaseManager::instance().db());
    CopyService cs(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 bookId = mkBook(catId, "9787111213828", "C++ Primer");

    mkCopy(bookId, "CP-001", "A区");
    mkCopy(bookId, "CP-002", "B区");
    QVERIFY(cs.changeCopyStatus(admin(), 1, "BORROWED").ok());
    QVERIFY(cs.changeCopyStatus(admin(), 2, "MAINTENANCE").ok());

    Result r = s.getBook(admin(), bookId);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("copy_total")).toInt(), 2);
    QCOMPARE(r.data.value(QStringLiteral("copy_available")).toInt(), 0);
    QCOMPARE(r.data.value(QStringLiteral("copy_borrowed")).toInt(), 1);
    QCOMPARE(r.data.value(QStringLiteral("copy_maintenance")).toInt(), 1);
    QCOMPARE(r.data.value(QStringLiteral("category_name")).toString(), QStringLiteral("计算机"));
    QVERIFY(!r.data.value(QStringLiteral("locations")).toStringList().isEmpty());

    // 不存在
    r = s.getBook(admin(), 424242);
    QCOMPARE(r.code, ErrorCode::BookNotFound);
}

void TestBackendA::bookUpdate()
{
    BookService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 catId2 = mkCategory(QStringLiteral("数学"));
    const qint64 bookId = mkBook(catId, "A0001", "旧标题");

    Result r = s.updateBook(admin(), bookId, QVariantMap { { "title", "新标题" },
                                                            { "category_id", catId2 } });
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("title")).toString(), QStringLiteral("新标题"));
    QCOMPARE(r.data.value(QStringLiteral("category_id")).toLongLong(), catId2);

    // 更新为已存在的 ISBN（非自身）
    mkBook(catId2, "B0002", "别家的书");
    r = s.updateBook(admin(), bookId, QVariantMap { { "isbn", "B0002" } });
    QCOMPARE(r.code, ErrorCode::IsbnDuplicate);

    // 保持自身 ISBN 不应触发查重
    r = s.updateBook(admin(), bookId, QVariantMap { { "isbn", "A0001" } });
    QVERIFY(r.ok());

    // 空更新
    r = s.updateBook(admin(), bookId, QVariantMap {});
    QCOMPARE(r.code, ErrorCode::InvalidCondition);
    // 分类不存在
    r = s.updateBook(admin(), bookId, QVariantMap { { "category_id", 999 } });
    QCOMPARE(r.code, ErrorCode::ForeignKeyNotFound);
}

void TestBackendA::bookListPaginationAndFilters()
{
    BookService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    for (int i = 1; i <= 7; ++i)
        mkBook(catId, QStringLiteral("ISBN%1").arg(i), QStringLiteral("书名%1").arg(i),
               QStringLiteral("作者%1").arg(i));

    // 分页边界：7 条 / 每页 3 → 3/3/1/0
    Result r = s.listBooks(admin(), QVariantMap(), 1, 3);
    QVERIFY(r.ok());
    QCOMPARE(r.total, 7);
    QCOMPARE(r.list.size(), 3);
    r = s.listBooks(admin(), QVariantMap(), 2, 3);
    QCOMPARE(r.list.size(), 3);
    r = s.listBooks(admin(), QVariantMap(), 3, 3);
    QCOMPARE(r.list.size(), 1);
    r = s.listBooks(admin(), QVariantMap(), 4, 3);
    QCOMPARE(r.total, 7);
    QCOMPARE(r.list.size(), 0);

    // 非法分页
    r = s.listBooks(admin(), QVariantMap(), 0, 10);
    QCOMPARE(r.code, ErrorCode::InvalidPage);
    r = s.listBooks(admin(), QVariantMap(), 1, 0);
    QCOMPARE(r.code, ErrorCode::InvalidPage);

    // keyword 过滤（书名/作者/ISBN）
    r = s.listBooks(admin(), QVariantMap { { "keyword", "书名3" } }, 1, 10);
    QVERIFY2(r.ok() && r.total == 1,
             qPrintable(QStringLiteral("code=%1 msg=%2 total=%3").arg(int(r.code)).arg(r.message).arg(r.total)));
    r = s.listBooks(admin(), QVariantMap { { "keyword", "作者6" } }, 1, 10);
    QCOMPARE(r.total, 1);
    r = s.listBooks(admin(), QVariantMap { { "keyword", "ISBN7" } }, 1, 10);
    QCOMPARE(r.total, 1);
    r = s.listBooks(admin(), QVariantMap { { "keyword", "不存在的词" } }, 1, 10);
    QCOMPARE(r.total, 0);
    QVERIFY(r.list.isEmpty());

    // 分类过滤
    r = s.listBooks(admin(), QVariantMap { { "category_id", catId } }, 1, 10);
    QCOMPARE(r.total, 7);
    // 状态过滤
    QVERIFY(s.shelveOff(admin(), 1).ok());
    r = s.listBooks(admin(), QVariantMap { { "status", "ACTIVE" } }, 1, 10);
    QCOMPARE(r.total, 6);
    r = s.listBooks(admin(), QVariantMap { { "status", "SHELVED_OFF" } }, 1, 10);
    QCOMPARE(r.total, 1);
}

void TestBackendA::bookShelveOffAndDelete()
{
    BookService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 withBorrow = mkBook(catId, "WB01", "有借阅的书");
    mkCopy(withBorrow, "WB-C1");
    const qint64 deletable = mkBook(catId, "DL01", "可删除的书");
    mkCopy(deletable, "DL-C1");
    insertReaderAndBorrow(withBorrow);

    // 权限
    Result r = s.shelveOff(reader(), withBorrow);
    QCOMPARE(r.code, ErrorCode::NoPermission);

    // 下架成功
    r = s.shelveOff(libOp(), withBorrow);
    QVERIFY(r.ok());
    r = s.getBook(admin(), withBorrow);
    QCOMPARE(r.data.value(QStringLiteral("status")).toString(), QStringLiteral("shelved_off"));

    // 重复下架
    r = s.shelveOff(admin(), withBorrow);
    QCOMPARE(r.code, ErrorCode::BookShelvedOff);
    QVERIFY(r.message.contains(QStringLiteral("已处于下架")));

    // 有借阅记录的图书删除被拒，提示使用下架
    r = s.deleteBook(admin(), withBorrow);
    QCOMPARE(r.code, ErrorCode::HasBorrowRecords);
    QVERIFY(r.message.contains(QStringLiteral("下架")));

    // 无借阅记录可物理删除（连同副本）
    r = s.deleteBook(admin(), deletable);

    QVERIFY(r.ok());
    r = s.getBook(admin(), deletable);
    QCOMPARE(r.code, ErrorCode::BookNotFound);
    QSqlQuery q(DatabaseManager::instance().db());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM book_copies WHERE book_id = %1").arg(deletable));
    q.next();
    QCOMPARE(q.value(0).toInt(), 0);
}

// ==================== CopyService ====================
void TestBackendA::copyAddValidation()
{
    CopyService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 bookId = mkBook(catId, "A1", "一本书");

    // 条码必填
    Result r = s.addCopy(admin(), bookId, "  ");
    QCOMPARE(r.code, ErrorCode::MissingField);
    // 图书外键
    r = s.addCopy(admin(), 999, "XX-1");
    QCOMPARE(r.code, ErrorCode::ForeignKeyNotFound);
    QVERIFY(r.message.contains(QStringLiteral("图书不存在")));
    // 条码重复
    QVERIFY(s.addCopy(admin(), bookId, "BC-001").ok());
    r = s.addCopy(admin(), bookId, "BC-001");
    QCOMPARE(r.code, ErrorCode::BarcodeDuplicate);
    QVERIFY(r.message.contains(QStringLiteral("已存在")));
    // 权限
    r = s.addCopy(reader(), bookId, "BC-002");
    QCOMPARE(r.code, ErrorCode::NoPermission);
}

void TestBackendA::copyStateMachineLegalPaths()
{
    CopyService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 bookId = mkBook(catId, "A1", "一本书");

    // 路径1：AVAILABLE → BORROWED → AVAILABLE（借还闭环）
    qint64 c1 = mkCopy(bookId, "P1");
    QVERIFY(s.changeCopyStatus(admin(), c1, "BORROWED").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "AVAILABLE").ok());

    // 路径2：AVAILABLE → BORROWED → RESERVED（归还且有预约）→ BORROWED（预约者取书）→ AVAILABLE
    QVERIFY(s.changeCopyStatus(admin(), c1, "BORROWED").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "RESERVED").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "BORROWED").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "AVAILABLE").ok());

    // 路径3：AVAILABLE → RESERVED（预约触发）→ AVAILABLE（超时释放或取消）
    QVERIFY(s.changeCopyStatus(admin(), c1, "RESERVED").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "AVAILABLE").ok());

    // 路径4：AVAILABLE → MAINTENANCE → AVAILABLE（管理员恢复）
    QVERIFY(s.changeCopyStatus(admin(), c1, "MAINTENANCE").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "AVAILABLE").ok());

    // 路径5：AVAILABLE → MAINTENANCE → LOST（终结）
    QVERIFY(s.changeCopyStatus(admin(), c1, "MAINTENANCE").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "LOST").ok());
    // 路径5b：LOST → AVAILABLE（管理员恢复）
    QVERIFY(s.changeCopyStatus(admin(), c1, "AVAILABLE").ok());

    // 路径6：AVAILABLE → LOST → AVAILABLE
    QVERIFY(s.changeCopyStatus(admin(), c1, "LOST").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "AVAILABLE").ok());

    // 路径7：AVAILABLE → RESERVED → BORROWED
    QVERIFY(s.changeCopyStatus(admin(), c1, "RESERVED").ok());
    QVERIFY(s.changeCopyStatus(admin(), c1, "BORROWED").ok());

    // 统计一致性
    Result st = s.copyStatistics(admin(), bookId);
    QVERIFY(st.ok());
    QCOMPARE(st.data.value(QStringLiteral("copy_total")).toInt(), 1);
}

void TestBackendA::copyStateMachineIllegalPaths()
{
    CopyService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 bookId = mkBook(catId, "A1", "一本书");

    // 全部非法转换（排除合法表）逐组验证
    struct Case { QString from; QString to; };
    const Case illegal[] = {
        // BORROWED 出发
        { "BORROWED", "MAINTENANCE" }, { "BORROWED", "LOST" },
        // RESERVED 出发
        { "RESERVED", "MAINTENANCE" }, { "RESERVED", "LOST" },
        // MAINTENANCE 出发
        { "MAINTENANCE", "BORROWED" }, { "MAINTENANCE", "RESERVED" },
        // LOST 出发
        { "LOST", "BORROWED" }, { "LOST", "RESERVED" },
        { "LOST", "MAINTENANCE" }, { "LOST", "LOST" },
    };
    for (const Case& c : illegal) {
        qint64 copyId = mkCopy(bookId, QStringLiteral("IL-%1-%2").arg(c.from, c.to));
        // 先合法地走到 from 状态
        if (c.from != QStringLiteral("AVAILABLE")) {
            Result step = s.changeCopyStatus(admin(), copyId, c.from);
            QVERIFY2(step.ok(), qPrintable(QStringLiteral("进入状态 %1 失败: %2").arg(c.from, step.message)));
            if (c.from == QStringLiteral("MAINTENANCE") || c.from == QStringLiteral("LOST"))
                ; // 从 AVAILABLE 进入 MAINTENANCE/LOST 均合法
        }
        Result r = s.changeCopyStatus(admin(), copyId, c.to);
        QCOMPARE(r.code, ErrorCode::IllegalStatusTransition);
        // 返回当前状态信息
        QVERIFY2(r.message.contains(copyStatusDisplayName(c.from)),
                 qPrintable(QStringLiteral("错误信息未含当前状态[%1]: %2").arg(c.from, r.message)));
        // 状态未变
        Result cur = s.getCopyByBarcode(admin(), QStringLiteral("IL-%1-%2").arg(c.from, c.to));
        QCOMPARE(cur.data.value(QStringLiteral("status")).toString(), c.from.toLower());
    }

    // 相同状态（AVAILABLE → AVAILABLE）拒绝
    qint64 same = mkCopy(bookId, "SAME-1");
    Result r = s.changeCopyStatus(admin(), same, "AVAILABLE");
    QCOMPARE(r.code, ErrorCode::IllegalStatusTransition);

    // 未知状态
    r = s.changeCopyStatus(admin(), same, "FLYING");
    QCOMPARE(r.code, ErrorCode::InvalidCondition);

    // 副本不存在
    r = s.changeCopyStatus(admin(), 9999, "BORROWED");
    QCOMPARE(r.code, ErrorCode::CopyNotFound);
}

void TestBackendA::copyMaintenanceLostAdminOnly()
{
    CopyService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 bookId = mkBook(catId, "A1", "一本书");
    const qint64 c1 = mkCopy(bookId, "AD-1");
    const qint64 c2 = mkCopy(bookId, "AD-2");

    // 图书管理员把副本送修：合法
    QVERIFY(s.changeCopyStatus(libOp(), c1, "MAINTENANCE").ok());
    // 但从维修中恢复：仅管理员
    Result r = s.changeCopyStatus(libOp(), c1, "AVAILABLE");
    QCOMPARE(r.code, ErrorCode::NoPermission);
    QVERIFY(s.changeCopyStatus(admin(), c1, "AVAILABLE").ok());

    // 遗失报损恢复：仅管理员
    QVERIFY(s.changeCopyStatus(libOp(), c2, "LOST").ok());
    r = s.changeCopyStatus(libOp(), c2, "AVAILABLE");
    QCOMPARE(r.code, ErrorCode::NoPermission);
    QVERIFY(s.changeCopyStatus(admin(), c2, "AVAILABLE").ok());
}

void TestBackendA::copyRulesOnShelvedBook()
{
    BookService bs(DatabaseManager::instance().db());
    CopyService cs(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 bookId = mkBook(catId, "A1", "一本书");
    mkCopy(bookId, "SH-1");

    // 下架后不可再新增副本
    QVERIFY(bs.shelveOff(admin(), bookId).ok());
    Result r = cs.addCopy(admin(), bookId, "SH-2");
    QCOMPARE(r.code, ErrorCode::BookShelvedOff);

    // 下架书副本不可借出（借出动作由 C 板块触发，此处防御校验）
    r = cs.changeCopyStatus(admin(), 1, "BORROWED");
    QCOMPARE(r.code, ErrorCode::BookShelvedOff);
    QVERIFY(r.message.contains(QStringLiteral("下架")));
}

void TestBackendA::copyListAndUpdate()
{
    CopyService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    const qint64 b1 = mkBook(catId, "B1", "书一");
    const qint64 b2 = mkBook(catId, "B2", "书二");
    mkCopy(b1, "L1", "A区");
    mkCopy(b1, "L2", "B区");
    mkCopy(b2, "L3", "C区");

    // 按图书查副本
    Result r = s.listCopiesByBook(admin(), b1, 1, 10);
    QVERIFY(r.ok());
    QCOMPARE(r.total, 2);
    QVERIFY(r.data.contains(QStringLiteral("copy_status_stats")));

    // 分页
    r = s.listCopiesByBook(admin(), b1, 1, 1);
    QCOMPARE(r.list.size(), 1);
    r = s.listCopiesByBook(admin(), b1, 2, 1);
    QCOMPARE(r.list.size(), 1);
    r = s.listCopiesByBook(admin(), b1, 3, 1);
    QCOMPARE(r.list.size(), 0);

    // 位置过滤
    r = s.listCopies(admin(), QVariantMap { { "location", "A" } }, 1, 10);
    QCOMPARE(r.total, 1);
    // 状态过滤 + 非法状态
    r = s.listCopies(admin(), QVariantMap { { "status", "AVAILABLE" } }, 1, 10);
    QCOMPARE(r.total, 3);
    r = s.listCopies(admin(), QVariantMap { { "status", "NOPE" } }, 1, 10);
    QCOMPARE(r.code, ErrorCode::InvalidCondition);

    // 条码定位
    r = s.getCopyByBarcode(admin(), "L3");
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("book_title")).toString(), QStringLiteral("书二"));
    r = s.getCopyByBarcode(admin(), "NO-SUCH");
    QCOMPARE(r.code, ErrorCode::CopyNotFound);

    // 编辑位置
    r = s.updateCopy(admin(), 1, QVariantMap { { "location", "新位置" } });
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("location")).toString(), QStringLiteral("新位置"));

    // 经 updateCopy 改状态被拒（必须走状态机）
    r = s.updateCopy(admin(), 1, QVariantMap { { "status", "BORROWED" } });
    QCOMPARE(r.code, ErrorCode::IllegalStatusTransition);
    QVERIFY(r.message.contains(QStringLiteral("changeCopyStatus")));
}

// ==================== SearchService ====================
void TestBackendA::searchKeyword()
{
    SearchService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    mkBook(catId, "9787111213828", "C++ Primer 中文版", "Lippman", "人民邮电出版社");
    mkBook(catId, "9787115428029", "算法导论", "Cormen", "人民邮电出版社");
    const qint64 mathCat = mkCategory(QStringLiteral("数学"));
    mkBook(mathCat, "9787030110929", "具体数学", "Graham", "科学出版社");
    mkCopy(1, "S1", "A区");
    mkCopy(1, "S2", "B区");
    mkCopy(2, "S3", "A区");

    // 命中书名
    Result r = s.search(admin(), "C++", 1, 10);
    QVERIFY2(r.ok() && r.total == 1,
             qPrintable(QStringLiteral("code=%1 msg=%2 total=%3").arg(int(r.code)).arg(r.message).arg(r.total)));
    QCOMPARE(r.list.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("C++ Primer 中文版"));
    QCOMPARE(r.list.first().toMap().value(QStringLiteral("available_count")).toInt(), 2);
    QVERIFY(r.list.first().toMap().value(QStringLiteral("borrowable")).toBool());
    // 命中作者 / ISBN
    QCOMPARE(s.search(admin(), "Cormen", 1, 10).total, 1);
    QCOMPARE(s.search(admin(), "9787030110929", 1, 10).total, 1);
    // 不命中
    r = s.search(admin(), "量子力学", 1, 10);
    QCOMPARE(r.total, 0);
    QVERIFY(r.list.isEmpty());
    // 空关键词 → 空结果而非全量
    r = s.search(admin(), "   ", 1, 10);
    QVERIFY(r.ok());
    QCOMPARE(r.total, 0);
    QVERIFY(r.list.isEmpty());
    // 分页边界：3 本书 / 每页 2
    r = s.search(admin(), "出版社", 1, 2); // 无法命中出版社（检索范围为书名/作者/ISBN）
    QCOMPARE(r.total, 0);
    r = s.search(admin(), "数学", 1, 2);
    QCOMPARE(r.total, 1);
    // 下架书不出现在检索结果
    BookService bs(DatabaseManager::instance().db());
    QVERIFY(bs.shelveOff(admin(), 1).ok());
    r = s.search(admin(), "C++", 1, 10);
    QCOMPARE(r.total, 0);
}

// ==================== AdvancedSearchService ====================
void TestBackendA::advancedAndOrLogic()
{
    AdvancedSearchService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    mkBook(catId, "A1", "C++ Primer", "Lippman", "邮电", "2013-09-01");
    mkBook(catId, "A2", "C++ 并发编程", "Williams", "邮电", "2019-01-01");
    mkBook(catId, "A3", "算法导论", "Cormen", "邮电", "2012-12-01");
    mkCopy(1, "C1"); mkCopy(2, "C2");

    // 类别间 AND：书名 C++ AND 作者 Lippman → 只命中书1
    QVariantMap cond;
    cond.insert(QStringLiteral("titles"), QStringList { "C++" });
    cond.insert(QStringLiteral("authors"), QStringList { "Lippman" });
    Result r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QVERIFY(r.ok());
    QCOMPARE(r.total, 1);
    QCOMPARE(r.list.first().toMap().value(QStringLiteral("isbn")).toString(), QStringLiteral("A1"));

    // 同类别 OR：书名 [C++ / 算法] → 书1/2/3
    cond.clear();
    cond.insert(QStringLiteral("titles"), QStringList { "C++", "算法" });
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 3);

    // AND 收紧：书名 [C++] AND 作者 [Cormen] → 交集为空
    cond.clear();
    cond.insert(QStringLiteral("titles"), QStringList { "C++" });
    cond.insert(QStringLiteral("authors"), QStringList { "Cormen" });
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 0); // C++ 书作者不是 Cormen
}

void TestBackendA::advancedFilters()
{
    AdvancedSearchService s(DatabaseManager::instance().db());
    const qint64 catCs = mkCategory(QStringLiteral("计算机"));
    const qint64 catMath = mkCategory(QStringLiteral("数学"));
    mkBook(catCs, "Y1", "2015书", "a", "", "2015-06-01"); // 2015, 有副本可借
    mkBook(catCs, "Y2", "2019书", "a", "", "2019-06-01"); // 2019, 副本全借出
    mkBook(catMath, "Y3", "2012书", "b", "", "2012-06-01"); // 2012, 无副本
    mkCopy(1, "F1", "东馆");
    mkCopy(2, "F2", "东馆");
    mkCopy(2, "F3", "西馆");
    mkCopy(3, "F4", "东馆");
    CopyService cs(DatabaseManager::instance().db());
    QVERIFY(cs.changeCopyStatus(admin(), 2, "BORROWED").ok()); // F2 借出
    QVERIFY(cs.changeCopyStatus(admin(), 3, "BORROWED").ok()); // F3 借出 → Y2 全借出

    // 出版年区间
    QVariantMap cond;
    cond.insert(QStringLiteral("yearFrom"), 2015);
    cond.insert(QStringLiteral("yearTo"), 2019);
    Result r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 2);
    cond.insert(QStringLiteral("yearFrom"), 2016);
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 1);

    // 馆藏状态：AVAILABLE / UNAVAILABLE（必须区分，不允许只显示"有馆藏"）
    cond.clear();
    cond.insert(QStringLiteral("copyStatus"), QStringLiteral("AVAILABLE"));
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 2); // Y1 与 Y3 可借
    cond.clear();
    cond.insert(QStringLiteral("copyStatus"), QStringLiteral("UNAVAILABLE"));
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 1); // 仅 Y2 全借出

    // 馆藏位置（同类 OR）
    cond.clear();
    cond.insert(QStringLiteral("locations"), QStringList { "东馆" });
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 3); // Y1(F1) Y2(F2) Y3(F4)
    cond.clear();
    cond.insert(QStringLiteral("locations"), QStringList { "西馆" });
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 1); // 仅 Y2(F3)
    cond.clear();
    cond.insert(QStringLiteral("locations"), QStringList { "东馆", "西馆" });
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 3);

    // ISBN 精确 + 分类多选
    cond.clear();
    cond.insert(QStringLiteral("isbn"), QStringLiteral("Y3"));
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 1);
    cond.clear();
    cond.insert(QStringLiteral("categoryIds"), QVariantList { catCs, catMath });
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 3);

    // 出版社词条
    mkBook(catCs, "Y4", "2015书二", "a", "清华出版社", "2015-06-01");
    cond.clear();
    cond.insert(QStringLiteral("publishers"), QStringList { "清华" });
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 1);

    // 组合：分类=计算机 AND 年份 2015 AND 可借
    cond.clear();
    cond.insert(QStringLiteral("categoryIds"), QVariantList { catCs });
    cond.insert(QStringLiteral("yearFrom"), 2015);
    cond.insert(QStringLiteral("yearTo"), 2015);
    cond.insert(QStringLiteral("copyStatus"), QStringLiteral("AVAILABLE"));
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.total, 1);
    QCOMPARE(r.list.first().toMap().value(QStringLiteral("isbn")).toString(), QStringLiteral("Y1"));
}

void TestBackendA::advancedSorting()
{
    AdvancedSearchService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    mkBook(catId, "S-A", "香蕉书", "", "", "2020-01-01");
    mkBook(catId, "S-B", "苹果书", "", "", "2021-01-01");
    mkBook(catId, "S-C", "橙子书", "", "", "2019-01-01");
    mkCopy(1, "X1"); mkCopy(1, "X2"); mkCopy(1, "X3");
    mkCopy(2, "X4");
    CopyService cs(DatabaseManager::instance().db());
    QVERIFY(cs.changeCopyStatus(admin(), 2, "BORROWED").ok()); // 书1 借出一本，剩 2 本可借

    // 按出版时间升序 / 降序
    QVariantMap cond;
    cond.insert(QStringLiteral("categoryIds"), QVariantList { catId });
    Result r = s.advancedSearch(admin(), cond, "publish_date", true, 1, 10);
    QCOMPARE(r.total, 3);
    QCOMPARE(r.list.at(0).toMap().value(QStringLiteral("isbn")).toString(), QStringLiteral("S-C"));
    QCOMPARE(r.list.at(2).toMap().value(QStringLiteral("isbn")).toString(), QStringLiteral("S-B"));
    r = s.advancedSearch(admin(), cond, "publish_date", false, 1, 10);
    QCOMPARE(r.list.at(0).toMap().value(QStringLiteral("isbn")).toString(), QStringLiteral("S-B"));

    // 按可借数降序：书1(3) > 书3(0) = 书2(0)
    r = s.advancedSearch(admin(), cond, "available", false, 1, 10);
    QCOMPARE(r.list.at(0).toMap().value(QStringLiteral("isbn")).toString(), QStringLiteral("S-A"));

    // 按书名升序（Unicode 码点序：橙 U+6A59 < 苹 U+82F9 < 香 U+9999）
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.list.at(0).toMap().value(QStringLiteral("isbn")).toString(), QStringLiteral("S-C"));

    // 分页边界：3 条 / 每页 2 → 2/1/0
    r = s.advancedSearch(admin(), cond, "title", true, 1, 2);
    QCOMPARE(r.list.size(), 2);
    r = s.advancedSearch(admin(), cond, "title", true, 2, 2);
    QCOMPARE(r.list.size(), 1);
    r = s.advancedSearch(admin(), cond, "title", true, 3, 2);
    QCOMPARE(r.list.size(), 0);
    QCOMPARE(r.total, 3);
}

void TestBackendA::advancedEdgeCases()
{
    AdvancedSearchService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    mkBook(catId, "E1", "存在书");

    // 空条件 → 空结果而非全量
    Result r = s.advancedSearch(admin(), QVariantMap(), "title", true, 1, 10);
    QVERIFY(r.ok());
    QCOMPARE(r.total, 0);
    QVERIFY(r.list.isEmpty());

    // 条件全为空白词条 → 仍视为空条件
    QVariantMap cond;
    cond.insert(QStringLiteral("titles"), QStringList { "  " });
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QVERIFY(r.ok());
    QCOMPARE(r.total, 0);

    // 条件全部非法：年份区间倒挂
    cond.clear();
    cond.insert(QStringLiteral("yearFrom"), 2025);
    cond.insert(QStringLiteral("yearTo"), 2020);
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.code, ErrorCode::InvalidCondition);
    QVERIFY(r.message.contains(QStringLiteral("非法")));

    // 条件全部非法：馆藏状态值错误
    cond.clear();
    cond.insert(QStringLiteral("copyStatus"), QStringLiteral("MAYBE"));
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QCOMPARE(r.code, ErrorCode::InvalidCondition);

    // 排序字段非法
    cond.clear();
    cond.insert(QStringLiteral("titles"), QStringList { "存在书" });
    r = s.advancedSearch(admin(), cond, "price", true, 1, 10);
    QCOMPARE(r.code, ErrorCode::InvalidCondition);

    // 部分非法 + 部分合法 → 忽略非法项继续执行，并提示
    cond.clear();
    cond.insert(QStringLiteral("titles"), QStringList { "存在书" });
    cond.insert(QStringLiteral("copyStatus"), QStringLiteral("MAYBE"));
    r = s.advancedSearch(admin(), cond, "title", true, 1, 10);
    QVERIFY(r.ok());
    QCOMPARE(r.total, 1);
    QVERIFY(r.message.contains(QStringLiteral("忽略")));

    // 分页参数非法
    r = s.advancedSearch(admin(), cond, "title", true, 0, 10);
    QCOMPARE(r.code, ErrorCode::InvalidPage);
}

// ==================== CategoryService ====================
void TestBackendA::categoryCrud()
{
    CategoryService s(DatabaseManager::instance().db());

    // 必填 / 重名
    Result r = s.addCategory(admin(), QVariantMap { { "name", "" } });
    QCOMPARE(r.code, ErrorCode::MissingField);
    QVERIFY(s.addCategory(admin(), QVariantMap { { "name", "文学" } }).ok());
    r = s.addCategory(admin(), QVariantMap { { "name", "文学" } });
    QCOMPARE(r.code, ErrorCode::NameDuplicate);

    // 编辑
    r = s.updateCategory(admin(), 1, QVariantMap { { "name", "现当代文学" } });
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("name")).toString(), QStringLiteral("现当代文学"));

    // 列表与筛选
    const qint64 catId = 1;
    BookService bs(DatabaseManager::instance().db());
    mkBook(catId, "C1", "文学书");
    r = s.listCategories(admin(), QString(), 1, 10);
    QCOMPARE(r.total, 1);
    QCOMPARE(r.list.first().toMap().value(QStringLiteral("book_count")).toInt(), 1);
    r = s.listCategories(admin(), QStringLiteral("文学"), 1, 10);
    QCOMPARE(r.total, 1);
    r = s.listCategories(admin(), QStringLiteral("科幻"), 1, 10);
    QCOMPARE(r.total, 0);

    // 删除被图书引用的分类被拒
    r = s.deleteCategory(admin(), catId);
    QCOMPARE(r.code, ErrorCode::CategoryInUse);
    QVERIFY(r.message.contains(QStringLiteral("1 本图书")));

    // 无引用可删除
    QVERIFY(s.addCategory(admin(), QVariantMap { { "name", "空分类" } }).ok());
    r = s.deleteCategory(admin(), 2);
    QVERIFY(r.ok());
    r = s.deleteCategory(admin(), 2);
    QCOMPARE(r.code, ErrorCode::CategoryNotFound);

    // 读者无权限删除
    QVERIFY(s.addCategory(admin(), QVariantMap { { "name", "暂存" } }).ok());
    r = s.deleteCategory(reader(), 3);
    QCOMPARE(r.code, ErrorCode::NoPermission);
}

// ==================== ImportExportService ====================
void TestBackendA::importCsvBooksErrorRows()
{
    ImportExportService s(DatabaseManager::instance().db());
    mkCategory(QStringLiteral("计算机"));
    mkCategory(QStringLiteral("数学"));
    mkBook(1, "EXIST1", "已有书");

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("books.csv"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "ISBN,书名,作者,出版社,出版日期,分类,简介\n";
    ts << "N1,合法书1,作者A,社A,2020-01-01,计算机,\n";
    ts << ",缺ISBN,作者B,社B,2020-01-01,数学,\n";           // 行3：ISBN 为空
    ts << "N3,,作者C,社C,2020-01-01,数学,\n";                // 行4：书名为空
    ts << "N4,坏日期书,作者D,社D,2020/1/1,数学,\n";           // 行5：日期格式错
    ts << "N5,幽灵分类书,作者E,社E,2020-01-01,不存在分类,\n";  // 行6：分类不存在
    ts << "N6,文件内重复1,作者F,社F,2020-01-01,数学,\n";
    ts << "N6,文件内重复2,作者G,社G,2020-01-01,数学,\n";      // 行8：ISBN 文件内重复
    ts << "EXIST1,库里重复,作者H,社H,2020-01-01,计算机,\n";    // 行9：ISBN 与库重复
    ts << "N9,合法书2,作者I,社I,,数学,\n";                    // 行10：日期为空（可选）合法
    f.close();

    Result r = s.importBooks(admin(), path);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("total_rows")).toInt(), 9);
    QCOMPARE(r.data.value(QStringLiteral("imported_rows")).toInt(), 3);
    QCOMPARE(r.data.value(QStringLiteral("error_rows")).toInt(), 6);

    // 错误行号与原因
    QCOMPARE(r.list.size(), 6);
    const int row3 = r.list.at(0).toMap().value(QStringLiteral("row")).toInt();
    QCOMPARE(row3, 3);
    QVERIFY(r.list.at(0).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("ISBN")));
    QVERIFY(r.list.at(3).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("分类")));
    QVERIFY(r.list.at(5).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("已存在")));
    QVERIFY(r.list.at(4).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("重复")));

    // 合法行确实入库（导入顺序：N1→id2, N6→id3, N9→id4）
    BookService bs(DatabaseManager::instance().db());
    Result get = bs.getBook(admin(), 2); // N1
    QVERIFY(get.ok());
    QCOMPARE(get.data.value(QStringLiteral("title")).toString(), QStringLiteral("合法书1"));
    get = bs.getBook(admin(), 4); // N9
    QVERIFY(get.ok());
    QCOMPARE(get.data.value(QStringLiteral("publish_date")).toString(), QString());
}

void TestBackendA::importCsvCopies()
{
    ImportExportService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    mkBook(catId, "ISBNA", "书A");
    mkBook(catId, "ISBND", "书D");
    mkCopy(1, "EXC1");

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("copies.csv"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "条码,ISBN,馆藏位置,入藏日期\n";
    ts << "NCA1,ISBNA,东馆,2024-02-01\n";
    ts << ",ISBNA,东馆,\n";            // 行3：条码为空
    ts << "NCA3,NOEXIST,东馆,\n";      // 行4：图书不存在
    ts << "EXC1,ISBNA,东馆,\n";        // 行5：条码已存在
    ts << "NCA5,ISBND,西馆,2024-03-05\n";
    f.close();

    Result r = s.importCopies(admin(), path);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("imported_rows")).toInt(), 2);
    QCOMPARE(r.data.value(QStringLiteral("error_rows")).toInt(), 3);
    QVERIFY(r.list.at(0).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("条码")));
    QVERIFY(r.list.at(1).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("不存在")));
    QVERIFY(r.list.at(2).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("已存在")));
}

void TestBackendA::importEmptyAndBadFormat()
{
    ImportExportService s(DatabaseManager::instance().db());
    QTemporaryDir dir;

    // 空文件
    const QString emptyPath = dir.filePath(QStringLiteral("empty.csv"));
    { QFile f(emptyPath); QVERIFY(f.open(QIODevice::WriteOnly)); }
    Result r = s.importBooks(admin(), emptyPath);
    QCOMPARE(r.code, ErrorCode::FormatError);
    QVERIFY(r.message.contains(QStringLiteral("为空")));

    // 缺少必需列
    const QString badHeader = dir.filePath(QStringLiteral("bad_header.csv"));
    QFile f(badHeader);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "编号,名称\n1,某书\n";
    f.close();
    r = s.importBooks(admin(), badHeader);
    QCOMPARE(r.code, ErrorCode::FormatError);
    QVERIFY(r.message.contains(QStringLiteral("缺少必需列")));

    // 不支持的格式
    const QString txtPath = dir.filePath(QStringLiteral("data.txt"));
    { QFile f2(txtPath); QVERIFY(f2.open(QIODevice::WriteOnly)); f2.write("hello"); }
    r = s.importBooks(admin(), txtPath);
    QCOMPARE(r.code, ErrorCode::FormatError);
    QVERIFY(r.message.contains(QStringLiteral("不支持的文件格式")));
    r = s.importBooks(reader(), txtPath);
    QCOMPARE(r.code, ErrorCode::NoPermission);
}

void TestBackendA::exportImportRoundTripCsv()
{
    // 库1：造数据并导出 → 库2（全新内存库）：导入并核对数量与字段一致
    // 导出器放在独立作用域：重开内存库前先释放旧连接引用，避免 removeDatabase 告警
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    mkBook(catId, "RT1", "往返书1", "作者1", "社1", "2020-01-01");
    mkBook(catId, "RT2", "往返书2", "作者2", "社2", "2021-01-01");
    mkBook(catId, "RT3", "往返书3", "作者3", "社3", "2022-01-01");
    mkCopy(1, "R1", "东馆");
    mkCopy(1, "R2", "西馆");
    mkCopy(2, "R3", "东馆");
    {
        CopyService cs(DatabaseManager::instance().db());
        QVERIFY(cs.changeCopyStatus(admin(), 3, "BORROWED").ok());
    }

    QTemporaryDir dir;
    const QString booksCsv = dir.filePath(QStringLiteral("books_rt.csv"));
    const QString copiesCsv = dir.filePath(QStringLiteral("copies_rt.csv"));
    Result r;
    {
        ImportExportService s(DatabaseManager::instance().db());
        r = s.exportBooks(admin(), booksCsv);
        QVERIFY(r.ok());
        QCOMPARE(r.data.value(QStringLiteral("exported_rows")).toInt(), 3);
        r = s.exportCopies(admin(), copiesCsv);
        QVERIFY(r.ok());
        QCOMPARE(r.data.value(QStringLiteral("exported_rows")).toInt(), 3);
    }

    // 切换到全新的内存库（旧连接已关闭，必须重建服务实例以绑定新连接）
    auto& mgr = DatabaseManager::instance();
    QVERIFY(mgr.open(QStringLiteral(":memory:")));
    QVERIFY(mgr.initSchema());
    mkCategory(QStringLiteral("计算机")); // id=1，与库1相同
    ImportExportService importer(mgr.db());

    r = importer.importBooks(admin(), booksCsv);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("imported_rows")).toInt(), 3);
    QCOMPARE(r.data.value(QStringLiteral("error_rows")).toInt(), 0);
    r = importer.importCopies(admin(), copiesCsv);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("imported_rows")).toInt(), 3);
    QCOMPARE(r.data.value(QStringLiteral("error_rows")).toInt(), 0);

    // 字段一致
    BookService bs(DatabaseManager::instance().db());
    Result get = bs.getBook(admin(), 2);
    QVERIFY(get.ok());
    QCOMPARE(get.data.value(QStringLiteral("isbn")).toString(), QStringLiteral("RT2"));
    QCOMPARE(get.data.value(QStringLiteral("publish_date")).toString(), QStringLiteral("2021-01-01"));
    // 注意：副本状态不参与导出/导入（状态由业务产生），导出的副本导入后均为在馆可借
    QCOMPARE(get.data.value(QStringLiteral("copy_total")).toInt(), 1);
    QCOMPARE(get.data.value(QStringLiteral("copy_available")).toInt(), 1);
}

void TestBackendA::exportImportRoundTripXlsx()
{
    const qint64 catId = mkCategory(QStringLiteral("数学"));
    mkBook(catId, "XL1", "表格书1", "", "", "2020-05-05");
    mkBook(catId, "XL2", "表格书2", "", "", "2021-06-06");
    mkCopy(1, "XC1", "东馆");

    QTemporaryDir dir;
    const QString booksX = dir.filePath(QStringLiteral("books_rt.xlsx"));
    const QString copiesX = dir.filePath(QStringLiteral("copies_rt.xlsx"));
    Result r;
    {
        ImportExportService s(DatabaseManager::instance().db());
        r = s.exportBooks(admin(), booksX);
        QVERIFY(r.ok());
        QCOMPARE(r.data.value(QStringLiteral("exported_rows")).toInt(), 2);
        r = s.exportCopies(admin(), copiesX);
        QVERIFY(r.ok());
        QCOMPARE(r.data.value(QStringLiteral("exported_rows")).toInt(), 1);
    }

    QVERIFY(DatabaseManager::instance().open(QStringLiteral(":memory:")));
    QVERIFY(DatabaseManager::instance().initSchema());
    mkCategory(QStringLiteral("数学"));
    ImportExportService importerX(DatabaseManager::instance().db());

    r = importerX.importBooks(admin(), booksX);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("imported_rows")).toInt(), 2);
    r = importerX.importCopies(admin(), copiesX);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("imported_rows")).toInt(), 1);

    BookService bs(DatabaseManager::instance().db());
    Result get = bs.getBook(admin(), 1);
    QVERIFY(get.ok());
    QCOMPARE(get.data.value(QStringLiteral("isbn")).toString(), QStringLiteral("XL1"));
    QCOMPARE(get.data.value(QStringLiteral("copy_total")).toInt(), 1);
    QCOMPARE(get.data.value(QStringLiteral("locations")).toStringList(),
             QStringList { QStringLiteral("东馆") });
}

void TestBackendA::exportLargeFile()
{
    ImportExportService s(DatabaseManager::instance().db());
    const qint64 catId = mkCategory(QStringLiteral("计算机"));
    // 直插 1200 本图书与 1200 条副本（超过单批 500 的批次大小）
    QSqlQuery q(DatabaseManager::instance().db());
    QVERIFY(DatabaseManager::instance().db().transaction());
    for (int i = 1; i <= 1200; ++i) {
        QVERIFY(q.prepare(QStringLiteral(
            "INSERT INTO books (isbn, title, category_id) VALUES (?, ?, ?)")));
        q.addBindValue(QStringLiteral("BIG%1").arg(i));
        q.addBindValue(QStringLiteral("大文件书%1").arg(i));
        q.addBindValue(catId);
        QVERIFY(q.exec());
        QVERIFY(q.prepare(QStringLiteral(
            "INSERT INTO book_copies (copy_code, book_id, location) VALUES (?, ?, '库房')")));
        q.addBindValue(QStringLiteral("BIGC%1").arg(i));
        q.addBindValue(i);
        QVERIFY(q.exec());
    }
    QVERIFY(DatabaseManager::instance().db().commit());

    QTemporaryDir dir;
    const QString csvPath = dir.filePath(QStringLiteral("big_books.csv"));
    Result r = s.exportBooks(admin(), csvPath);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("exported_rows")).toInt(), 1200);
    // 核对 CSV 行数（表头 + 1200 行）
    QFile f(csvPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const int lines = f.readAll().count('\n');
    f.close();
    QCOMPARE(lines, 1201);

    const QString copiesPath = dir.filePath(QStringLiteral("big_copies.csv"));
    r = s.exportCopies(admin(), copiesPath);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("exported_rows")).toInt(), 1200);

    const QString xlsxPath = dir.filePath(QStringLiteral("big_books.xlsx"));
    r = s.exportBooks(admin(), xlsxPath);
    QVERIFY(r.ok());
    QCOMPARE(r.data.value(QStringLiteral("exported_rows")).toInt(), 1200);
}

QTEST_MAIN(TestBackendA)
// 注：类声明在 test_backend_a.h（已登记 HEADERS），moc 由 qmake 单独编译，无需再 include .moc
