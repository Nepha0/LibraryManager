/*
 * test_backend_c.cpp —— 后端C 测试
 *
 * A 组：无库冒烟（契约路径：操作者校验/缺参/占位码）
 * B 组：SQLite 内存库全链路（借→还→续借→预约全流转→逾期→越权→超时失效顺延）
 *       复用 A 端 DatabaseManager（SQLite schema 与统一 MySQL DDL 对齐）。
 * MySQL 全量回归（并发双借乐观锁等）按契约 §11 联调阶段在 MySQL 冒烟库补齐。
 */
#include "contract_c.h"
#include "database/DatabaseManager.h"
#include "services/CopyService.h"   // A 端 CopyService（INCLUDEPATH 含 library-backend-a/src）
#include "services/BorrowService.h"
#include "services/ReturnService.h"
#include "services/RenewService.h"
#include "services/ReservationService.h"
#include "services/OverdueService.h"
#include "services/PersonalService.h"
#include "services/RecommendService.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <cstdio>

namespace {

int g_failures = 0;

void expect(bool cond, const char* what)
{
    if (cond) {
        std::printf("  [PASS] %s\n", what);
    } else {
        ++g_failures;
        std::printf("  [FAIL] %s\n", what);
    }
}

int codeOf(const lib::Result& r) { return static_cast<int>(r.code); }

// 简单取单值
QVariant scalar(const QSqlDatabase& db, const QString& sql, const QVariantList& binds = {})
{
    QSqlQuery q(db);
    q.prepare(sql);
    for (const QVariant& b : binds) q.addBindValue(b);
    q.exec();
    return q.next() ? q.value(0) : QVariant();
}

qint64 scalarId(const QSqlDatabase& db, const QString& sql, const QVariantList& binds = {})
{ return scalar(db, sql, binds).toLongLong(); }

lib::OperatorContext makeStaff()
{
    lib::OperatorContext op;
    op.userId = 999;
    op.userName = QStringLiteral("测试管理员");
    op.role = lib::Role::Admin;
    op.authenticated = true;
    return op;
}

lib::OperatorContext makeReader(qint64 userId)
{
    lib::OperatorContext op;
    op.userId = userId;
    op.userName = QStringLiteral("测试读者");
    op.role = lib::Role::Reader;
    op.authenticated = true;
    return op;
}

// ===================== B 组：SQLite 全链路 =====================
void runDbTests()
{
    std::printf("-- SQLite 全链路 --\n");
    lib::DatabaseManager& dbm = lib::DatabaseManager::instance();
    expect(dbm.open(QStringLiteral(":memory:")), "打开 SQLite 内存库");
    QString schemaErr;
    expect(dbm.initSchema(&schemaErr), qPrintable(QStringLiteral("建表: %1").arg(schemaErr)));
    const QSqlDatabase db = dbm.db();

    // 种子数据：分类/图书/2副本/2读者（R001 绑定 user_id=1）/系统参数
    QSqlQuery q(db);
    q.exec(QStringLiteral("INSERT INTO categories(name) VALUES('文学')"));
    q.exec(QStringLiteral("INSERT INTO books(isbn,title,author,category_id,status) "
                          "VALUES('978-7-000-000001-1','测试图书A','作者甲',1,'active')"));
    const qint64 bookId = scalarId(db, QStringLiteral("SELECT id FROM books LIMIT 1"));
    q.exec(QStringLiteral("INSERT INTO book_copies(copy_code, book_id, location) VALUES('C001',%1,'A区')").arg(bookId));
    q.exec(QStringLiteral("INSERT INTO book_copies(copy_code, book_id, location) VALUES('C002',%1,'B区')").arg(bookId));
    q.exec(QStringLiteral("INSERT INTO users(username, display_name, password_hash, salt, role) "
                          "VALUES('readeru1','读者一','x','y','reader')"));
    q.exec(QStringLiteral("INSERT INTO readers(user_id, reader_no, name, phone) VALUES(1,'R001','读者一','13800000001')"));
    q.exec(QStringLiteral("INSERT INTO readers(reader_no, name, phone) VALUES('R002','读者二','13800000002')"));
    const qint64 r1 = scalarId(db, QStringLiteral("SELECT id FROM readers WHERE reader_no='R001'"));
    const qint64 r2 = scalarId(db, QStringLiteral("SELECT id FROM readers WHERE reader_no='R002'"));
    const char* params[] = {"borrow_period_days", "30", "borrow_limit", "5", "max_renew_times", "1",
                            "reservation_hold_days", "3", "renew_period_days", "15"};
    for (int i = 0; i < 10; i += 2)
        q.exec(QStringLiteral("INSERT OR REPLACE INTO system_params(param_key, param_value) VALUES('%1','%2')")
                   .arg(params[i], params[i + 1]));

    lib::CopyService copies(db);
    lib::EligibilityFn okEligibility = [](qint64) {
        lib::EligibilityResult e;
        e.eligible = true;
        return e;
    };
    lib::BorrowService borrow(db, &copies, okEligibility);
    lib::ReturnService ret(db, &copies);
    lib::RenewService renew(db, okEligibility);
    lib::ReservationService resv(db, &copies);
    lib::OverdueService overdue(db);
    lib::PersonalService personal(db);

    const lib::OperatorContext staff = makeStaff();
    const lib::OperatorContext readerOp = makeReader(1);
    const QDate today = QDate::currentDate();
    const QString iso = QStringLiteral("yyyy-MM-dd");

    // 0) 有可借副本时不可预约
    expect(codeOf(resv.reserve(staff, r1, bookId)) == 3006, "有可借副本时预约 → 3006");

    // 1) 正常借阅
    lib::Result b1 = borrow.borrow(staff, r1, QStringLiteral("C001"));
    expect(b1.ok(), qPrintable(QStringLiteral("借阅 C001: %1").arg(b1.message)));
    expect(b1.data.value(QStringLiteral("due_at")).toString() == today.addDays(30).toString(iso),
           "应还日期 = 借出日 + 30 天");
    const qint64 loan1 = b1.data.value(QStringLiteral("loan_id")).toLongLong();
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C001'")).toString()
               == QStringLiteral("borrowed"), "副本 C001 → borrowed");

    // 2) 并发/重复借同一副本被拒（双保险，乐观锁在联调层验证）
    expect(codeOf(borrow.borrow(staff, r2, QStringLiteral("C001"))) == 3002, "重复借同一副本 → 3002");

    // 3) 第二副本
    lib::Result b2 = borrow.borrow(staff, r1, QStringLiteral("C002"));
    expect(b2.ok(), qPrintable(QStringLiteral("借阅 C002: %1").arg(b2.message)));
    const qint64 loan2 = b2.data.value(QStringLiteral("loan_id")).toLongLong();

    // 4) 续借
    lib::Result rn1 = renew.renew(staff, loan1);
    expect(rn1.ok(), qPrintable(QStringLiteral("续借 loan1: %1").arg(rn1.message)));
    expect(rn1.data.value(QStringLiteral("new_due_at")).toString() == today.addDays(45).toString(iso),
           "新应还日期 = 原应还 + 15 天");
    expect(rn1.data.value(QStringLiteral("renew_count")).toInt() == 1, "续借次数 = 1");
                qPrintable(scalar(db, QStringLiteral("SELECT renew_count FROM loans WHERE id=%1").arg(loan1)).toString()),
                qPrintable(scalar(db, QStringLiteral("SELECT due_at FROM loans WHERE id=%1").arg(loan1)).toString()),
                qPrintable(scalar(db, QStringLiteral("SELECT param_value FROM system_params WHERE param_key='max_renew_times'")).toString()));

    // 5) 续借超次数（max_renew_times=1）
    lib::Result rn2 = renew.renew(staff, loan1);
    expect(codeOf(rn2) == 3004, "二次续借 → 3004");
    expect(codeOf(renew.renew(staff, 99999)) == 3003, "不存在记录续借 → 3003");

    // 6) 全部借出后可预约；排队与重复排队
    lib::Result rs1 = resv.reserve(staff, r1, bookId);
    expect(rs1.ok(), qPrintable(QStringLiteral("R001 预约: %1").arg(rs1.message)));
    expect(rs1.data.value(QStringLiteral("queue_no")).toInt() == 1, "排队号 = 1");
    expect(codeOf(resv.reserve(staff, r1, bookId)) == 3005, "同书重复排队 → 3005");
    lib::Result rs2 = resv.reserve(staff, r2, bookId);
    expect(rs2.ok() && rs2.data.value(QStringLiteral("queue_no")).toInt() == 2, "R002 排队号 = 2");

    // 7) 归还触发队首转待取
    lib::Result rt1 = ret.returnBook(staff, {{QStringLiteral("loan_id"), loan1}});
    expect(rt1.ok(), qPrintable(QStringLiteral("归还 loan1: %1").arg(rt1.message)));
    expect(rt1.data.value(QStringLiteral("reservation_triggered")).toBool(), "归还触发预约转待取");
    expect(rt1.data.value(QStringLiteral("copy_status")).toString() == QStringLiteral("reserved"),
           "副本 C001 → reserved");
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE id=%1").arg(
                      rs1.data.value(QStringLiteral("reservation_id")).toLongLong())).toString()
               == QStringLiteral("ready_to_pick"), "队首预约 → ready_to_pick");
    expect(rt1.data.value(QStringLiteral("overdue_days")).toInt() == 0, "未逾期归还 overdue_days=0");

    // 8) 预约者取书（reserved → borrowed）
    lib::Result b3 = borrow.borrow(staff, r1, QStringLiteral("C001"));
    expect(b3.ok(), qPrintable(QStringLiteral("预约者取书借阅: %1").arg(b3.message)));
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE id=%1").arg(
                      rs1.data.value(QStringLiteral("reservation_id")).toLongLong())).toString()
               == QStringLiteral("finished"), "待取预约 → finished");

    // 9) 归还第二本触发下一位
    lib::Result rt2 = ret.returnBook(staff, {{QStringLiteral("copy_barcode"), QStringLiteral("C002")}});
    expect(rt2.ok() && rt2.data.value(QStringLiteral("reservation_triggered")).toBool(),
           qPrintable(QStringLiteral("按条码归还触发 R002 待取: %1").arg(rt2.message)));
    const qint64 rs2Id = rs2.data.value(QStringLiteral("reservation_id")).toLongLong();

    // 10) 待取取消 → 副本释放回馆
    expect(resv.cancelReservation(staff, rs2Id).ok(), "取消待取预约");
    expect(scalar(db, QStringLiteral("SELECT status FROM book_copies WHERE copy_code='C002'")).toString()
               == QStringLiteral("available"), "副本 C002 → available");
    expect(codeOf(resv.cancelReservation(staff, rs2Id)) == 3008, "终态预约再取消 → 3008");

    // 11) 逾期：构造过期在借记录（借出 40 天前、应还 10 天前）
    const qint64 c2 = scalarId(db, QStringLiteral("SELECT id FROM book_copies WHERE copy_code='C002'"));
    expect(q.exec(QStringLiteral("INSERT INTO loans(reader_id, copy_id, book_id, borrowed_at, due_at) "
                          "VALUES(%1, %2, %3, '%4', '%5')")
               .arg(r2).arg(c2).arg(bookId)
               .arg(today.addDays(-40).toString(iso), today.addDays(-10).toString(iso))),
          qPrintable(QStringLiteral("夹具：插入逾期在借记录 err=%1").arg(q.lastError().text())));
    q.exec(QStringLiteral("UPDATE book_copies SET status='borrowed' WHERE copy_code='C002'"));
    lib::Result od = overdue.listOverdue(staff, {{QStringLiteral("mode"), QStringLiteral("current")}});
    expect(od.ok() && od.total == 1, "逾期在借识别 1 条");
    if (!od.list.isEmpty())
        expect(od.list.first().toMap().value(QStringLiteral("overdue_days")).toInt() == 10, "逾期天数 = 10");
    expect(overdue.listOverdue(makeReader(1), {}).code == lib::ErrorCode::NoPermission,
           "读者查看逾期管理 → 1002");

    // 12) 存在未处理逾期时续借被拒
    const qint64 overdueLoan = scalarId(db, QStringLiteral(
        "SELECT id FROM loans WHERE reader_id=%1 AND returned_at IS NULL").arg(r2));
    lib::Result rnBad = renew.renew(staff, overdueLoan);
    expect(codeOf(rnBad) == 3004 && rnBad.message.contains(QStringLiteral("逾期")),
           qPrintable(QStringLiteral("逾期续借 → 3004（%1）").arg(rnBad.message)));

    // 13) 个人中心
    lib::Result po = personal.getPersonalOverview(staff, r1);
    expect(po.ok(), qPrintable(QStringLiteral("个人中心聚合: %1").arg(po.message)));
    expect(po.data.value(QStringLiteral("profile")).toMap().value(QStringLiteral("reader_no")).toString()
               == QStringLiteral("R001"), "profile.reader_no = R001");
    expect(!po.data.value(QStringLiteral("active_loans")).toList().isEmpty(), "在借列表非空");
    expect(!po.data.value(QStringLiteral("history")).toList().isEmpty(), "历史列表非空");
    expect(!po.data.value(QStringLiteral("reservations")).toList().isEmpty(), "预约列表非空");
    expect(codeOf(personal.getPersonalOverview(readerOp, r2)) == 1002, "读者越权查他人 → 1002");
    expect(personal.getPersonalOverview(readerOp, r1).ok(), "读者查本人 → 通过");

    // 14) 预约超时失效顺延：R002 排队 + 手工构造一条已超时待取（测试夹具直写状态）
    lib::Result rs3 = resv.reserve(staff, r2, bookId);
    expect(rs3.ok(), qPrintable(QStringLiteral("R002 再次排队: %1").arg(rs3.message)));
    expect(q.exec(QStringLiteral("UPDATE book_copies SET status='reserved' WHERE copy_code='C001'")),
           qPrintable(QStringLiteral("夹具：C001 置 reserved err=%1").arg(q.lastError().text())));
    expect(q.exec(QStringLiteral("INSERT INTO reservations(book_id, reader_id, queue_no, status, requested_at, pickup_deadline) "
                          "VALUES(%1, %2, 1, 'ready_to_pick', datetime('now','localtime'), datetime('now','localtime','-1 day'))")
               .arg(bookId).arg(r1)),
           qPrintable(QStringLiteral("夹具：插入超时待取 err=%1").arg(q.lastError().text())));
    lib::Result pm = resv.promoteNext(staff, bookId);
    expect(pm.ok(), qPrintable(QStringLiteral("超时释放并顺延到下一位: %1").arg(pm.message)));
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE status='expired'")).toString()
               == QStringLiteral("expired"), "超时待取 → expired");
    expect(scalar(db, QStringLiteral("SELECT status FROM reservations WHERE id=%1").arg(
                      rs3.data.value(QStringLiteral("reservation_id")).toLongLong())).toString()
               == QStringLiteral("ready_to_pick"), "顺延后下一位 → ready_to_pick");

    // 15) 队列与列表查询
    lib::Result queue = resv.listReservationQueue(staff, bookId);
    expect(queue.ok() && queue.total >= 3, qPrintable(QStringLiteral("队列表 total=%1").arg(queue.total)));
    lib::Result waitingList = resv.listReservations(staff, {{QStringLiteral("status"), QStringLiteral("ready_to_pick")}});
    expect(waitingList.ok() && waitingList.total == 1, "ready_to_pick 筛选 1 条");
    expect(resv.listReservations(staff, {{QStringLiteral("status"), QStringLiteral("bad_status")}}).code
               == lib::ErrorCode::InvalidCondition, "非法预约状态 → 1015");

    // 16) getLoan 与借阅记录筛选
    lib::Result gl = borrow.getLoan(staff, loan1);
    expect(gl.ok() && gl.data.value(QStringLiteral("status_text")).toString() == QStringLiteral("已还"),
           "getLoan(loan1) 已还");
    lib::Result actives = borrow.listLoans(staff, {{QStringLiteral("status"), QStringLiteral("active")}});
    expect(actives.ok() && actives.total == 2, qPrintable(QStringLiteral("在借筛选 2 条（R001×1 + 逾期 1）: %1").arg(actives.total)));
    lib::Result ods = borrow.listLoans(staff, {{QStringLiteral("status"), QStringLiteral("overdue")}});
    expect(ods.ok() && ods.total == 1, "overdue 筛选 1 条");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    std::printf("== 后端C 测试 ==\n");

    // ---------- A 组：无库冒烟 ----------
    std::printf("-- A 组：契约冒烟（无库） --\n");
    expect(codeOf(lib::failC(lib::ErrorCodeC::ReaderNotEligible, QStringLiteral("x"))) == 3001, "3001 ReaderNotEligible");
    expect(codeOf(lib::failC(lib::ErrorCodeC::NotImplemented, QStringLiteral("x"))) == 3999, "3999 NotImplemented");
    expect(lib::resvStatusToString(lib::ResvStatus::ReadyToPick) == QStringLiteral("ready_to_pick"),
           "预约状态字面量 ready_to_pick");

    QSqlDatabase invalidDb = QSqlDatabase();
    lib::CopyService copies(invalidDb);
    lib::BorrowService borrow(invalidDb, &copies, {});
    lib::OperatorContext anon;
    expect(codeOf(borrow.borrow(anon, 1, QStringLiteral("BC001"))) == 1001, "未登录 → 1001");
    lib::OperatorContext readerCtx = makeReader(1);
    expect(codeOf(borrow.borrow(readerCtx, 1, QStringLiteral("BC001"))) == 1002, "读者办理借阅 → 1002");
    lib::OperatorContext staff = makeStaff();
    expect(codeOf(borrow.borrow(staff, 0, QStringLiteral("BC001"))) == 1003, "缺读者 → 1003");
    expect(codeOf(borrow.borrow(staff, 1, QStringLiteral("  "))) == 1003, "缺条码 → 1003");
    lib::BorrowService borrowNo(invalidDb, &copies, [](qint64) {
        lib::EligibilityResult e;
        e.eligible = false;
        e.reasons << QStringLiteral("读者账号已停用");
        return e;
    });
    expect(codeOf(borrowNo.borrow(staff, 1, QStringLiteral("BC001"))) == 3001, "资格不通过 → 3001");
    lib::ReturnService ret(invalidDb, &copies);
    expect(codeOf(ret.returnBook(staff, {})) == 1003, "缺 loan_id/条码 → 1003");
    lib::RecommendService rec(invalidDb);
    expect(codeOf(rec.recommend(staff, 1)) == 3999, "recommend 骨架占位 → 3999");

    // ---------- B 组：SQLite 全链路 ----------
    runDbTests();

    std::printf("== %s ==\n", g_failures == 0 ? "Backend C smoke test passed." : "Backend C smoke test FAILED.");
    return g_failures == 0 ? 0 : 1;
}
